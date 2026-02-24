#!/usr/bin/env bash
set -euo pipefail

NWIPE_BIN="${1:-./build/nwipe}"
ARTIFACT_DIR="${NWIPE_CI_ARTIFACT_DIR:-}"

if [[ ! -x "${NWIPE_BIN}" ]]; then
    echo "Error: nwipe binary not executable: ${NWIPE_BIN}"
    exit 2
fi

if [[ "$(id -u)" -ne 0 ]]; then
    echo "Error: this script must run as root (loop devices + dmsetup + block wiping)."
    exit 1
fi

require_cmd() {
    local cmd="$1"
    if ! command -v "${cmd}" >/dev/null 2>&1; then
        echo "Error: required command not found: ${cmd}"
        exit 1
    fi
}

for cmd in losetup truncate dmsetup blockdev mktemp grep tail; do
    require_cmd "${cmd}"
done

WORKDIR="$(mktemp -d /tmp/nwipe-ci-fault.XXXXXX)"
BACKING_FILE="${WORKDIR}/disk.img"
LOG_DIR="${WORKDIR}/logs"
mkdir -p "${LOG_DIR}"

LOOP_DEV=""
DM_NAME="nwipe-ci-fault-$$"
DM_DEV="/dev/mapper/${DM_NAME}"

cleanup() {
    if dmsetup info "${DM_NAME}" >/dev/null 2>&1; then
        dmsetup remove "${DM_NAME}" >/dev/null 2>&1 || true
    fi

    if [[ -n "${LOOP_DEV}" ]]; then
        losetup -d "${LOOP_DEV}" >/dev/null 2>&1 || true
    fi

    if [[ -n "${ARTIFACT_DIR}" ]]; then
        mkdir -p "${ARTIFACT_DIR}"
        cp -a "${LOG_DIR}/." "${ARTIFACT_DIR}/" >/dev/null 2>&1 || true
    fi

    rm -rf "${WORKDIR}"
}
trap cleanup EXIT

assert_log_contains() {
    local log_file="$1"
    local pattern="$2"

    if ! grep -Fq "${pattern}" "${log_file}"; then
        echo "Error: '${pattern}' not found in ${log_file}"
        echo "--- tail ${log_file} ---"
        tail -n 120 "${log_file}" || true
        return 1
    fi
}

echo "Preparing faulty dmsetup device..."
truncate -s 64M "${BACKING_FILE}"
LOOP_DEV="$(losetup --find --show "${BACKING_FILE}")"
TOTAL_SECTORS="$(blockdev --getsz "${LOOP_DEV}")"

if [[ "${TOTAL_SECTORS}" -le 32 ]]; then
    echo "Error: loop device too small (${TOTAL_SECTORS} sectors)."
    exit 1
fi

BAD_START=8
BAD_LEN=8
PREFIX_LEN="${BAD_START}"
SUFFIX_START=$((BAD_START + BAD_LEN))
SUFFIX_LEN=$((TOTAL_SECTORS - SUFFIX_START))

DM_TABLE=""
if [[ "${PREFIX_LEN}" -gt 0 ]]; then
    DM_TABLE+="0 ${PREFIX_LEN} linear ${LOOP_DEV} 0"$'\n'
fi
DM_TABLE+="${BAD_START} ${BAD_LEN} error"$'\n'
if [[ "${SUFFIX_LEN}" -gt 0 ]]; then
    DM_TABLE+="${SUFFIX_START} ${SUFFIX_LEN} linear ${LOOP_DEV} ${SUFFIX_START}"$'\n'
fi

printf '%b' "${DM_TABLE}" | dmsetup create "${DM_NAME}"
echo "Faulty device: ${DM_DEV}"

LOG_FILE="${LOG_DIR}/fault_injection.log"
STDOUT_FILE="${LOG_DIR}/fault_injection.stdout"
STDERR_FILE="${LOG_DIR}/fault_injection.stderr"

set +e
"${NWIPE_BIN}" \
    --autonuke \
    --nogui \
    --nowait \
    --nosignals \
    --cachedio \
    --noblank \
    --rounds=1 \
    --sync=0 \
    --verify=off \
    --method=zero \
    --prng=isaac \
    --PDFreportpath=noPDF \
    --logfile="${LOG_FILE}" \
    "${DM_DEV}" >"${STDOUT_FILE}" 2>"${STDERR_FILE}"
RC=$?
set -e

if [[ "${RC}" -eq 0 ]]; then
    echo "Error: fault injection run unexpectedly succeeded."
    echo "--- tail ${STDOUT_FILE} ---"
    tail -n 120 "${STDOUT_FILE}" || true
    echo "--- tail ${STDERR_FILE} ---"
    tail -n 120 "${STDERR_FILE}" || true
    exit 1
fi

assert_log_contains "${LOG_FILE}" "Nwipe exited with errors"
echo "Fault injection test passed (expected failure behavior observed)."
