#!/usr/bin/env bash
set -euo pipefail

MODE="${1:-full}"
NWIPE_BIN="${2:-./src/nwipe}"
ARTIFACT_DIR="${NWIPE_CI_ARTIFACT_DIR:-}"
STS_BIN="${NWIPE_STS_BIN:-}"
STS_MIN_RATIO="${NWIPE_STS_MIN_RATIO:-0.9}"
STS_ITERATIONS="${NWIPE_STS_ITERATIONS:-4}"
STS_RETRIES="${NWIPE_STS_RETRIES:-0}"

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
    local attempt
    local max_attempts
    local last_error

    if [[ -z "${STS_BIN}" ]]; then
        return 0
    fi

    if [[ ! -x "${STS_BIN}" ]]; then
        echo "Error: STS binary not executable: ${STS_BIN}"
        return 1
    fi

    max_attempts=$((STS_RETRIES + 1))
    last_error=""
    mkdir -p "${LOG_DIR}/${case_name}.sts"

    for (( attempt=1; attempt<=max_attempts; attempt++ )); do
        local attempt_dir="${LOG_DIR}/${case_name}.sts/attempt-${attempt}"
        mkdir -p "${attempt_dir}"
        result_file="${attempt_dir}/result.txt"
        sts_log="${attempt_dir}/run.log"

        echo "==> Running STS for case ${case_name} (attempt ${attempt}/${max_attempts}, min ratio ${STS_MIN_RATIO}, iterations ${STS_ITERATIONS})"

        set +e
        "${STS_BIN}" -v 1 -i "${STS_ITERATIONS}" -I 1 -w "${attempt_dir}" -F r "${BACKING_FILE}" \
            > >(tee "${sts_log}") 2>&1
        local sts_rc=$?
        set -e

        if [[ "${sts_rc}" -ne 0 ]]; then
            last_error="STS run failed for ${case_name} with rc=${sts_rc}"
        elif [[ ! -f "${result_file}" ]]; then
            last_error="STS result file missing for ${case_name}: ${result_file}"
        else
            passed="$(awk '/tests passed successfully both the analyses/ { split($1,a,"/"); print a[1]; exit }' "${result_file}")"
            total="$(awk '/tests passed successfully both the analyses/ { split($1,a,"/"); print a[2]; exit }' "${result_file}")"

            if [[ -z "${passed}" || -z "${total}" ]]; then
                last_error="unable to parse STS pass ratio from ${result_file}"
            else
                ratio="$(awk -v p="${passed}" -v t="${total}" 'BEGIN{ if (t == 0) { print "0.0" } else { printf "%.6f", p/t } }')"
                ratio_ok="$(awk -v r="${ratio}" -v m="${STS_MIN_RATIO}" 'BEGIN{ if (r+0 >= m+0) print "1"; else print "0" }')"

                echo "STS ratio for ${case_name}: ${passed}/${total} = ${ratio}"
                if [[ "${ratio_ok}" == "1" ]]; then
                    if [[ "${attempt}" -gt 1 ]]; then
                        echo "Warning: STS for ${case_name} passed on retry ${attempt}/${max_attempts}"
                    fi
                    return 0
                fi
                last_error="STS ratio ${ratio} is below threshold ${STS_MIN_RATIO} for ${case_name}"
            fi
        fi

        if [[ "${attempt}" -lt "${max_attempts}" ]]; then
            echo "Warning: ${last_error}. Retrying STS for ${case_name}..."
        fi
    done

    echo "Error: ${last_error}"
    return 1
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
    local io="$2"
    local method="$3"
    local verify="$4"
    local prng="${5:-isaac}"
    local reverse="${6:-0}"

    local log_file="${LOG_DIR}/${case_name}.log"
    local stdout_file="${LOG_DIR}/${case_name}.stdout"
    local stderr_file="${LOG_DIR}/${case_name}.stderr"

    local reverse_flag=""
    if [[ "${reverse}" -eq 1 ]]; then
        reverse_flag="--reverse"
    fi

    echo "==> Running case: ${case_name} (io=${io} method=${method}, verify=${verify}, prng=${prng})"

    set +e
    "${NWIPE_BIN}" \
        --autonuke \
        --nogui \
        --nowait \
        --nosignals \
        --noblank \
        --rounds=1 \
        --${io} \
        --verify="${verify}" \
        --method="${method}" \
        --prng="${prng}" \
        ${reverse_flag} \
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

# Zero wipe + zero verify, direct I/O
run_nwipe_case "wipe_zero" "directio" "zero" "all"
assert_block_is_byte "00"
run_nwipe_case "verify_zero" "directio" "verify_zero" "off"

# Zero wipe + zero verify, cached I/O
run_nwipe_case "wipe_zero" "cachedio" "zero" "all"
assert_block_is_byte "00"
run_nwipe_case "verify_zero" "cachedio" "verify_zero" "off"

if [[ "${MODE}" == "full" ]]; then
    # One wipe + one verify, direct I/O
    run_nwipe_case "wipe_one" "directio" "one" "all"
    assert_block_is_byte "ff"
    run_nwipe_case "verify_one" "directio" "verify_one" "off"

    # One wipe + one verify, cached I/O
    run_nwipe_case "wipe_one" "cachedio" "one" "all"
    assert_block_is_byte "ff"
    run_nwipe_case "verify_one" "cachedio" "verify_one" "off"

    # PRNG wipe + PRNG verification, direct + cached I/O
    run_nwipe_case "wipe_prng" "directio" "prng" "all"
    run_nwipe_case "wipe_prng" "cachedio" "prng" "all"

    # Run --reverse tests (different routines), direct + cached I/O:
    run_nwipe_case "reverse_wipe_one" "directio" "one" "all" "isaac" 1
    run_nwipe_case "reverse_wipe_prng" "directio" "prng" "all" "isaac" 1
    run_nwipe_case "reverse_wipe_one" "cachedio" "one" "all" "isaac" 1
    run_nwipe_case "reverse_wipe_prng" "cachedio" "prng" "all" "isaac" 1

    # PRNG statistical cases (STS)
    # Run these in direct I/O so we're not verifying a cache
    echo "==> Running PRNG Stream coverage cases (each PRNG once)"
    run_nwipe_case "prng_stream_twister" "directio" "prng" "all" "twister"
    run_sts_ratio_check "prng_stream_twister"
    run_nwipe_case "prng_stream_isaac" "directio" "prng" "all" "isaac"
    run_sts_ratio_check "prng_stream_isaac"
    run_nwipe_case "prng_stream_isaac64" "directio" "prng" "all" "isaac64"
    run_sts_ratio_check "prng_stream_isaac64"
    run_nwipe_case "prng_stream_alfg" "directio" "prng" "all" "add_lagg_fibonacci_prng"
    run_sts_ratio_check "prng_stream_alfg"
    run_nwipe_case "prng_stream_xoroshiro256" "directio" "prng" "all" "xoroshiro256_prng"
    run_sts_ratio_check "prng_stream_xoroshiro256"
    run_nwipe_case "prng_stream_splitmix64" "directio" "prng" "all" "splitmix64"
    run_sts_ratio_check "prng_stream_splitmix64"
    run_nwipe_case "prng_stream_chacha20" "directio" "prng" "all" "chacha20"
    run_sts_ratio_check "prng_stream_chacha20"

    if cpu_supports_aes_ni; then
        run_nwipe_case "prng_stream_aes_ctr" "directio" "prng" "all" "aes_ctr_prng"
        run_sts_ratio_check "prng_stream_aes_ctr"
    else
        echo "Skipping aes_ctr_prng case: CPU does not expose AES-NI."
    fi
fi

echo "Loopback integration test suite passed (${MODE})."
