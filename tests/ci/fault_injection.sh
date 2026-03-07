#!/usr/bin/env bash
set -euo pipefail

NWIPE_BIN="${1:-./src/nwipe}"
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

for cmd in losetup truncate dmsetup blockdev mktemp grep tail tee; do
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

run_fault_case() {
    local case_name="$1"
    local io="$2"
    local method="${3:-zero}"
    local no_abort="${4:-0}"
    local no_abort_flag=""

    local log_file="${LOG_DIR}/${case_name}.log"
    local stdout_file="${LOG_DIR}/${case_name}.stdout"
    local stderr_file="${LOG_DIR}/${case_name}.stderr"

    if [[ "${no_abort}" -eq 1 ]]; then
        no_abort_flag="--no-abort-on-block-errors"
    fi

    echo "==> Running fault case: ${case_name} (io=${io}, method=${method}, no_abort=${no_abort})"

    set +e
    "${NWIPE_BIN}" \
        --autonuke \
        --nogui \
        --nowait \
        --nosignals \
        --noblank \
        --rounds=1 \
        --${io} \
        --verify=off \
        --method="${method}" \
        --prng=isaac \
        --PDFreportpath=noPDF \
        --logfile="${log_file}" \
        ${no_abort_flag} \
        "${DM_DEV}" \
        > >(tee "${stdout_file}") \
        2> >(tee "${stderr_file}" >&2)
    local rc=$?
    set -e

    if [[ "${rc}" -eq 0 ]]; then
        echo "Error: fault case '${case_name}' unexpectedly succeeded (rc=0)"
        echo "--- tail ${stdout_file} ---"
        tail -n 120 "${stdout_file}" || true
        echo "--- tail ${stderr_file} ---"
        tail -n 120 "${stderr_file}" || true
        return 1
    fi

    assert_log_contains "${log_file}" "Nwipe exited with errors"
    echo "    ${case_name}: correctly failed with rc=${rc}"
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
echo "Using nwipe binary: ${NWIPE_BIN}"
"${NWIPE_BIN}" --version || true

# Zero wipe - direct + cached I/O (both must abort)
run_fault_case "fault_zero_direct"  "directio" "zero" 0
run_fault_case "fault_zero_cached"  "cachedio" "zero" 0

# Zero wipe with --no-abort-on-block-errors (must still fail, but continues past errors)
run_fault_case "fault_zero_direct_no_abort"  "directio" "zero" 1
run_fault_case "fault_zero_cached_no_abort"  "cachedio" "zero" 1

echo ""
echo "Fault injection test suite passed (expected failure behavior observed)."
