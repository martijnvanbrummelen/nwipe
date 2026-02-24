#!/usr/bin/env bash
set -euo pipefail

MODE="${1:-full}"
NWIPE_BIN="${2:-./build/nwipe}"
ARTIFACT_DIR="${NWIPE_CI_ARTIFACT_DIR:-}"
STS_BIN="${NWIPE_STS_BIN:-}"
STS_MIN_RATIO="${NWIPE_STS_MIN_RATIO:-0.9}"
STS_ITERATIONS="${NWIPE_STS_ITERATIONS:-4}"

if [[ "${MODE}" != "full" && "${MODE}" != "smoke" ]]; then
    echo "Usage: $0 [full|smoke] [path-to-nwipe-binary]"
    exit 2
fi

if [[ ! -x "${NWIPE_BIN}" ]]; then
    echo "Error: nwipe binary not executable: ${NWIPE_BIN}"
    exit 2
fi

if [[ "$(id -u)" -ne 0 ]]; then
    echo "Error: this script must run as root (loop devices + block wiping)."
    exit 1
fi

require_cmd() {
    local cmd="$1"
    if ! command -v "${cmd}" >/dev/null 2>&1; then
        echo "Error: required command not found: ${cmd}"
        exit 1
    fi
}

for cmd in losetup truncate dd hexdump grep mktemp tail tee awk; do
    require_cmd "${cmd}"
done

WORKDIR="$(mktemp -d /tmp/nwipe-ci-loopback.XXXXXX)"
BACKING_FILE="${WORKDIR}/disk.img"
LOG_DIR="${WORKDIR}/logs"
mkdir -p "${LOG_DIR}"
LOOP_DEV=""

cleanup() {
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

run_sts_ratio_check() {
    local case_name="$1"
    local result_file
    local sts_log
    local passed
    local total
    local ratio
    local ratio_ok

    if [[ -z "${STS_BIN}" ]]; then
        return 0
    fi

    if [[ ! -x "${STS_BIN}" ]]; then
        echo "Error: STS binary not executable: ${STS_BIN}"
        return 1
    fi

    result_file="${LOG_DIR}/${case_name}.sts/result.txt"
    sts_log="${LOG_DIR}/${case_name}.sts/run.log"
    mkdir -p "${LOG_DIR}/${case_name}.sts"

    echo "==> Running STS for case ${case_name} (min ratio ${STS_MIN_RATIO}, iterations ${STS_ITERATIONS})"

    set +e
    "${STS_BIN}" -v 1 -i "${STS_ITERATIONS}" -I 1 -w "${LOG_DIR}/${case_name}.sts" -F r "${BACKING_FILE}" \
        > >(tee "${sts_log}") 2>&1
    local sts_rc=$?
    set -e

    if [[ "${sts_rc}" -ne 0 ]]; then
        echo "Error: STS run failed for ${case_name} with rc=${sts_rc}"
        return 1
    fi

    if [[ ! -f "${result_file}" ]]; then
        echo "Error: STS result file missing for ${case_name}: ${result_file}"
        return 1
    fi

    passed="$(awk '/tests passed successfully both the analyses/ { split($1,a,"/"); print a[1]; exit }' "${result_file}")"
    total="$(awk '/tests passed successfully both the analyses/ { split($1,a,"/"); print a[2]; exit }' "${result_file}")"

    if [[ -z "${passed}" || -z "${total}" ]]; then
        echo "Error: unable to parse STS pass ratio from ${result_file}"
        tail -n 80 "${result_file}" || true
        return 1
    fi

    ratio="$(awk -v p="${passed}" -v t="${total}" 'BEGIN{ if (t == 0) { print "0.0" } else { printf "%.6f", p/t } }')"
    ratio_ok="$(awk -v r="${ratio}" -v m="${STS_MIN_RATIO}" 'BEGIN{ if (r+0 >= m+0) print "1"; else print "0" }')"

    echo "STS ratio for ${case_name}: ${passed}/${total} = ${ratio}"
    if [[ "${ratio_ok}" != "1" ]]; then
        echo "Error: STS ratio ${ratio} is below threshold ${STS_MIN_RATIO} for ${case_name}"
        return 1
    fi
}

assert_block_is_byte() {
    local expected_byte="$1"
    local sample_hex
    sample_hex="$(dd if="${LOOP_DEV}" bs=4096 count=1 status=none | hexdump -v -e '/1 "%02x"')"

    if [[ "${expected_byte}" == "00" ]]; then
        [[ "${sample_hex}" =~ ^(00)+$ ]] || {
            echo "Error: first 4KiB block is not all 0x00"
            return 1
        }
        return 0
    fi

    if [[ "${expected_byte}" == "ff" ]]; then
        [[ "${sample_hex}" =~ ^(ff)+$ ]] || {
            echo "Error: first 4KiB block is not all 0xFF"
            return 1
        }
        return 0
    fi

    echo "Error: unsupported expected byte '${expected_byte}'"
    return 1
}

run_nwipe_case() {
    local case_name="$1"
    local method="$2"
    local verify="$3"
    local prng="${4:-isaac}"

    local log_file="${LOG_DIR}/${case_name}.log"
    local stdout_file="${LOG_DIR}/${case_name}.stdout"
    local stderr_file="${LOG_DIR}/${case_name}.stderr"

    echo "==> Running case: ${case_name} (method=${method}, verify=${verify}, prng=${prng})"

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
        --verify="${verify}" \
        --method="${method}" \
        --prng="${prng}" \
        --PDFreportpath=noPDF \
        --logfile="${log_file}" \
        "${LOOP_DEV}" \
        > >(tee "${stdout_file}") \
        2> >(tee "${stderr_file}" >&2)
    local rc=$?
    set -e

    if [[ "${rc}" -ne 0 ]]; then
        echo "Error: case '${case_name}' returned ${rc}, expected 0"
        echo "--- tail ${stdout_file} ---"
        tail -n 120 "${stdout_file}" || true
        echo "--- tail ${stderr_file} ---"
        tail -n 120 "${stderr_file}" || true
        return 1
    fi

    assert_log_contains "${log_file}" "Nwipe successfully completed."
}

cpu_supports_aes_ni() {
    grep -Eqi '(^|[[:space:]])aes([[:space:]]|$)' /proc/cpuinfo
}

echo "Preparing temporary loopback block device..."
truncate -s 32M "${BACKING_FILE}"
LOOP_DEV="$(losetup --find --show "${BACKING_FILE}")"
echo "Loop device: ${LOOP_DEV}"
echo "Using nwipe binary: ${NWIPE_BIN}"
"${NWIPE_BIN}" --version || true

run_nwipe_case "wipe_zero" "zero" "off"
assert_block_is_byte "00"

run_nwipe_case "verify_zero" "verify_zero" "off"

    if [[ "${MODE}" == "full" ]]; then
    run_nwipe_case "wipe_one" "one" "off"
    assert_block_is_byte "ff"

    run_nwipe_case "verify_one" "verify_one" "off"

    echo "==> Running PRNG Stream coverage cases (each PRNG once)"
    run_nwipe_case "prng_stream_twister" "prng" "off" "twister"
    run_sts_ratio_check "prng_stream_twister"
    run_nwipe_case "prng_stream_isaac" "prng" "off" "isaac"
    run_sts_ratio_check "prng_stream_isaac"
    run_nwipe_case "prng_stream_isaac64" "prng" "off" "isaac64"
    run_sts_ratio_check "prng_stream_isaac64"
    run_nwipe_case "prng_stream_alfg" "prng" "off" "add_lagg_fibonacci_prng"
    run_sts_ratio_check "prng_stream_alfg"
    run_nwipe_case "prng_stream_xoroshiro256" "prng" "off" "xoroshiro256_prng"
    run_sts_ratio_check "prng_stream_xoroshiro256"

    if cpu_supports_aes_ni; then
        run_nwipe_case "prng_stream_aes_ctr" "prng" "off" "aes_ctr_prng"
        run_sts_ratio_check "prng_stream_aes_ctr"
    else
        echo "Skipping aes_ctr_prng case: CPU does not expose AES-NI."
    fi
fi

echo "Loopback integration test suite passed (${MODE})."
