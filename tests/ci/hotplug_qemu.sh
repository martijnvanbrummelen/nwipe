#!/usr/bin/env bash
set -euo pipefail

NWIPE_BIN="${1:-./src/nwipe}"
ARTIFACT_DIR="${NWIPE_CI_ARTIFACT_DIR:-}"
ARCH="$(uname -m)"

# Resolve the newest matching kernel/initrd pair from the local machine.
default_boot_image() {
    local pattern
    local match

    for pattern in "$@"; do
        match="$(ls -1 ${pattern} 2>/dev/null | tail -n1 || true)"
        if [[ -n "${match}" ]]; then
            printf '%s\n' "${match}"
            return 0
        fi
    done

    return 1
}

case "${ARCH}" in
    x86_64|amd64)
        QEMU_BIN="${NWIPE_QEMU_BIN:-qemu-system-x86_64}"
        KERNEL_IMAGE="${NWIPE_QEMU_KERNEL:-$(default_boot_image /boot/vmlinuz-*amd64 /boot/vmlinuz-* || true)}"
        INITRD_IMAGE="${NWIPE_QEMU_INITRD:-$(default_boot_image /boot/initrd.img-*amd64 /boot/initrd.img-* || true)}"
        QEMU_MACHINE="${NWIPE_QEMU_MACHINE:-q35}"
        QEMU_CPU="${NWIPE_QEMU_CPU:-max}"
        QEMU_CONSOLE="${NWIPE_QEMU_CONSOLE:-ttyS0}"
        QEMU_HOTPLUG_DRIVER="${NWIPE_QEMU_HOTPLUG_DRIVER:-virtio-blk-pci}"
        QEMU_EXTRA_DEVICES=( -device pcie-root-port,id=hotplug-port,chassis=1,slot=1 )
        QEMU_DEVICE_BUS="hotplug-port"
        ;;
    *)
        echo "Error: unsupported architecture for QEMU hotplug test: ${ARCH}"
        exit 2
        ;;
esac

# Fail fast if the binary or boot assets are missing.
if [[ ! -x "${NWIPE_BIN}" ]]; then
    echo "Error: nwipe binary not executable: ${NWIPE_BIN}"
    exit 2
fi

if [[ ! -r "${KERNEL_IMAGE}" ]]; then
    echo "Error: kernel image not readable: ${KERNEL_IMAGE}"
    exit 2
fi

if [[ ! -r "${INITRD_IMAGE}" ]]; then
    echo "Error: initrd image not readable: ${INITRD_IMAGE}"
    exit 2
fi

require_cmd() {
    local cmd="$1"
    if ! command -v "${cmd}" >/dev/null 2>&1; then
        echo "Error: required command not found: ${cmd}"
        exit 1
    fi
}

# The harness depends on QEMU plus common archive and shell tools.
for cmd in "${QEMU_BIN}" cpio gzip xz bzip2 grep mktemp tail tee awk sed python3 cp chmod mkdir rm sort file zstd busybox; do
    require_cmd "${cmd}"
done

# Create a private workspace for the custom initrd, QMP socket, and logs.
WORKDIR="$(mktemp -d /tmp/nwipe-ci-qemu.XXXXXX)"
INITRD_DIR="${WORKDIR}/initrd"
SERIAL_LOG="${WORKDIR}/serial.log"
QMP_SOCK="${WORKDIR}/qmp.sock"
BACKING_FILE="${WORKDIR}/hotplug.img"
CUSTOM_INITRD="${WORKDIR}/custom-initrd.cpio.gz"
QEMU_PID=""

cleanup() {
    if [[ -n "${QEMU_PID}" ]] && kill -0 "${QEMU_PID}" >/dev/null 2>&1; then
        kill -TERM "${QEMU_PID}" >/dev/null 2>&1 || true
        wait "${QEMU_PID}" >/dev/null 2>&1 || true
    fi

    if [[ -n "${ARTIFACT_DIR}" ]]; then
        mkdir -p "${ARTIFACT_DIR}"
        for artifact in serial.log qemu.stdout qemu.stderr qmp-blockdev-add.json qmp-device-add.json; do
            if [[ -e "${WORKDIR}/${artifact}" ]]; then
                cp -a "${WORKDIR}/${artifact}" "${ARTIFACT_DIR}/" >/dev/null 2>&1 || true
            fi
        done
    fi

    rm -rf "${WORKDIR}"
}
trap cleanup EXIT

# Poll a log file until a marker appears or the timeout expires.
wait_for_log() {
    local log_file="$1"
    local pattern="$2"
    local timeout="${3:-120}"
    local i

    for ((i = 1; i <= timeout; i++)); do
        if grep -Fq "${pattern}" "${log_file}" 2>/dev/null; then
            return 0
        fi
        sleep 1
    done

    echo "Error: '${pattern}' not found in ${log_file}"
    echo "--- tail ${log_file} ---"
    tail -n 120 "${log_file}" || true
    return 1
}

# Copy nwipe and its shared library dependencies into the initrd tree.
copy_nwipe_deps() {
    local dep
    local copied=""

    while IFS= read -r dep; do
        if [[ -n "${dep}" && -e "${dep}" ]]; then
            if [[ "${copied}" != *" ${dep} "* ]]; then
                mkdir -p "${INITRD_DIR}$(dirname "${dep}")"
                cp -L "${dep}" "${INITRD_DIR}${dep}"
                copied="${copied} ${dep} "
            fi
        fi
    done < <(
        ldd "${NWIPE_BIN}" | awk '
            $1 ~ /^\// { print $1 }
            $3 ~ /^\// { print $3 }
        ' | sort -u
    )
}

# Rebuild the stock initrd with nwipe, busybox, and a tiny guest init script.
build_initrd() {
    mkdir -p "${INITRD_DIR}"
    case "$(file -b "${INITRD_IMAGE}")" in
        *Zstandard*|*zstd*)
            ( cd "${INITRD_DIR}" && zstd -dc "${INITRD_IMAGE}" | cpio -id --quiet )
            ;;
        *gzip*)
            ( cd "${INITRD_DIR}" && gzip -dc "${INITRD_IMAGE}" | cpio -id --quiet )
            ;;
        *XZ*|*xz*)
            ( cd "${INITRD_DIR}" && xz -dc "${INITRD_IMAGE}" | cpio -id --quiet )
            ;;
        *LZ4*|*lz4*)
            ( cd "${INITRD_DIR}" && lz4 -dc "${INITRD_IMAGE}" | cpio -id --quiet )
            ;;
        *bzip2*|*bzip*)
            ( cd "${INITRD_DIR}" && bzip2 -dc "${INITRD_IMAGE}" | cpio -id --quiet )
            ;;
        *cpio*|*archive*)
            ( cd "${INITRD_DIR}" && cpio -id --quiet < "${INITRD_IMAGE}" )
            ;;
        *)
            echo "Error: unsupported initrd compression for ${INITRD_IMAGE}"
            file -b "${INITRD_IMAGE}" || true
            exit 1
            ;;
    esac

    mkdir -p "${INITRD_DIR}/usr/local/bin"
    mkdir -p "${INITRD_DIR}/usr/bin"
    mkdir -p "${INITRD_DIR}/usr/sbin"
    mkdir -p "${INITRD_DIR}/sbin"
    mkdir -p "${INITRD_DIR}/tmp"
    mkdir -p "${INITRD_DIR}/bin"

    cp -a "${NWIPE_BIN}" "${INITRD_DIR}/usr/local/bin/nwipe"
    copy_nwipe_deps

    cp -a "$(command -v busybox)" "${INITRD_DIR}/bin/busybox"
    chmod +x "${INITRD_DIR}/bin/busybox"

    cat > "${INITRD_DIR}/usr/bin/which" <<'EOF'
#!/bin/busybox sh
for candidate in "$@"; do
    found=""
    case "$candidate" in
        /*)
            if [ -x "$candidate" ]; then
                echo "$candidate"
                exit 0
            fi
            ;;
        *)
            OLD_IFS="$IFS"
            IFS=:
            for dir in $PATH; do
                if [ -x "$dir/$candidate" ]; then
                    echo "$dir/$candidate"
                    exit 0
                fi
            done
            IFS="$OLD_IFS"
            ;;
    esac
done
exit 1
EOF
    chmod +x "${INITRD_DIR}/usr/bin/which"

    cat > "${INITRD_DIR}/usr/bin/hdparm" <<'EOF'
#!/bin/busybox sh
exit 0
EOF
    chmod +x "${INITRD_DIR}/usr/bin/hdparm"
    ln -sf /usr/bin/hdparm "${INITRD_DIR}/usr/local/bin/hdparm"
    ln -sf /usr/bin/hdparm "${INITRD_DIR}/usr/sbin/hdparm"
    ln -sf /usr/bin/hdparm "${INITRD_DIR}/sbin/hdparm"

    # Guest init mounts the minimal runtime, launches nwipe, and waits for
    # the hotplug wipe to complete before powering off.
    cat > "${INITRD_DIR}/init" <<'EOF'
#!/bin/busybox sh
set -eu

export HOME=/root
export PATH=/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export LD_LIBRARY_PATH=/usr/local/lib:/usr/lib/x86_64-linux-gnu:/lib/x86_64-linux-gnu:/usr/lib:/lib
export NWIPE_ALLOW_MISSING_HDPARM=1

/bin/busybox mount -t devtmpfs devtmpfs /dev

/bin/busybox mkdir -p /tmp /run /var/log /dev /proc /sys

/bin/busybox mount -t proc proc /proc
/bin/busybox mount -t sysfs sysfs /sys

/bin/busybox echo "guest-init: hdparm at /bin/hdparm? $(/bin/busybox test -x /bin/hdparm && echo yes || echo no)"
/bin/busybox echo "guest-init: which hdparm => $(/usr/bin/which hdparm 2>/dev/null || true)"

/usr/sbin/modprobe virtio_pci || true
/usr/sbin/modprobe virtio_blk || true
/usr/sbin/modprobe virtio_ring || true
/usr/sbin/modprobe libcrc32c || true

echo "guest-init: starting nwipe hotplug test"

/usr/local/bin/nwipe \
    --hotplug \
    --autonuke \
    --nogui \
    --cachedio \
    --method=zero \
    --verify=off \
    --noblank \
    --nowait \
    --nosignals \
    --PDFreportpath=noPDF \
    --logfile=/tmp/nwipe.log \
    >/tmp/nwipe.stdout 2>/tmp/nwipe.stderr &

NWIPE_PID=$!

/bin/busybox tail -n 0 -f /tmp/nwipe.log >/dev/console 2>/tmp/nwipe.tail.stderr &
TAIL_PID=$!

ready_timeout=120
ready_tick=0
while [ "${ready_tick}" -lt "${ready_timeout}" ]; do
    if /bin/busybox grep -Fq "hotplug: monitoring enabled" /tmp/nwipe.log 2>/dev/null && \
       /bin/busybox grep -Fq "hotplug: baseline snapshot captured" /tmp/nwipe.log 2>/dev/null; then
        echo "READY"
        break
    fi
    ready_tick=$((ready_tick + 1))
    /bin/busybox sleep 1
done

if [ "${ready_tick}" -ge "${ready_timeout}" ]; then
    echo "guest-init: nwipe did not become ready"
    /bin/busybox tail -n 120 /tmp/nwipe.log 2>/dev/null || true
    /bin/busybox echo "--- nwipe stdout ---"
    /bin/busybox tail -n 120 /tmp/nwipe.stdout 2>/dev/null || true
    /bin/busybox echo "--- nwipe stderr ---"
    /bin/busybox tail -n 120 /tmp/nwipe.stderr 2>/dev/null || true
    /bin/busybox kill -TERM "${TAIL_PID}" 2>/dev/null || true
    /bin/busybox kill -TERM "${NWIPE_PID}" 2>/dev/null || true
    wait "${NWIPE_PID}" 2>/dev/null || true
    /bin/busybox poweroff -f
fi

finish_timeout=240
finish_tick=0
while [ "${finish_tick}" -lt "${finish_timeout}" ]; do
    if /bin/busybox grep -Fq "hotplug: wipe completed successfully" /tmp/nwipe.log 2>/dev/null; then
        echo "DONE"
        break
    fi
    if ! /bin/busybox kill -0 "${NWIPE_PID}" 2>/dev/null; then
        echo "guest-init: nwipe exited unexpectedly"
        /bin/busybox tail -n 120 /tmp/nwipe.log 2>/dev/null || true
        /bin/busybox echo "--- nwipe stdout ---"
        /bin/busybox tail -n 120 /tmp/nwipe.stdout 2>/dev/null || true
        /bin/busybox echo "--- nwipe stderr ---"
        /bin/busybox tail -n 120 /tmp/nwipe.stderr 2>/dev/null || true
        /bin/busybox kill -TERM "${TAIL_PID}" 2>/dev/null || true
        /bin/busybox poweroff -f
    fi
    finish_tick=$((finish_tick + 1))
    /bin/busybox sleep 1
done

if [ "${finish_tick}" -ge "${finish_timeout}" ]; then
    echo "guest-init: timed out waiting for hotplug wipe completion"
    /bin/busybox tail -n 120 /tmp/nwipe.log 2>/dev/null || true
    /bin/busybox echo "--- nwipe stdout ---"
    /bin/busybox tail -n 120 /tmp/nwipe.stdout 2>/dev/null || true
    /bin/busybox echo "--- nwipe stderr ---"
    /bin/busybox tail -n 120 /tmp/nwipe.stderr 2>/dev/null || true
    /bin/busybox kill -TERM "${TAIL_PID}" 2>/dev/null || true
    /bin/busybox kill -TERM "${NWIPE_PID}" 2>/dev/null || true
    wait "${NWIPE_PID}" 2>/dev/null || true
    /bin/busybox poweroff -f
fi

/bin/busybox kill -TERM "${TAIL_PID}" 2>/dev/null || true
/bin/busybox kill -TERM "${NWIPE_PID}" 2>/dev/null || true
wait "${NWIPE_PID}" 2>/dev/null || true
/bin/busybox sync
/bin/busybox poweroff -f
EOF
    chmod +x "${INITRD_DIR}/init"

    ( cd "${INITRD_DIR}" && find . -print0 | cpio --null -o -H newc --quiet | gzip -9 > "${CUSTOM_INITRD}" )
}

# Open a QMP socket, enable capabilities, and send one command payload.
qmp_send() {
    local socket="$1"
    local json_payload="$2"

    python3 - "$socket" "$json_payload" <<'PY'
import json
import socket
import sys

sock_path = sys.argv[1]
payload = json.loads(sys.argv[2])
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(sock_path)
sock.recv(4096)
sock.sendall((json.dumps({"execute": "qmp_capabilities"}) + "\n").encode())
sock.recv(4096)
sock.sendall((json.dumps(payload) + "\n").encode())
resp = sock.recv(4096)
sys.stdout.write(resp.decode(errors="ignore"))
sock.close()
PY
}

# Build the guest rootfs image before starting the VM.
build_initrd

# Create the writable backing disk that will be attached after nwipe arms.
truncate -s 32M "${BACKING_FILE}"
dd if=/dev/urandom of="${BACKING_FILE}" bs=1M count=32 status=none

# Start QEMU with a serial log and a QMP socket so the test can inject disk hotplug.
"${QEMU_BIN}" \
    -machine "${QEMU_MACHINE}" \
    -cpu "${QEMU_CPU}" \
    -m 768 \
    -nographic \
    -serial file:"${SERIAL_LOG}" \
    -display none \
    -monitor none \
    -qmp unix:"${QMP_SOCK}",server=on,wait=off \
    "${QEMU_EXTRA_DEVICES[@]}" \
    -kernel "${KERNEL_IMAGE}" \
    -initrd "${CUSTOM_INITRD}" \
    -append "console=${QEMU_CONSOLE} rdinit=/init loglevel=4 panic=1" \
    >"${WORKDIR}/qemu.stdout" 2>"${WORKDIR}/qemu.stderr" &
QEMU_PID=$!

# Wait until the guest reports that hotplug monitoring is active.
wait_for_log "${SERIAL_LOG}" "READY" 180

# Add the raw backing file as a block node, then attach it as a virtual disk.
blockdev_resp="$(qmp_send "${QMP_SOCK}" \
    "{\"execute\":\"blockdev-add\",\"arguments\":{\"node-name\":\"hotplugdisk\",\"driver\":\"raw\",\"file\":{\"driver\":\"file\",\"filename\":\"${BACKING_FILE}\"}}}")"
printf '%s\n' "${blockdev_resp}" > "${WORKDIR}/qmp-blockdev-add.json"
if grep -Fq '"error"' "${WORKDIR}/qmp-blockdev-add.json"; then
    echo "Error: blockdev-add failed"
    cat "${WORKDIR}/qmp-blockdev-add.json"
    exit 1
fi

device_add_args="{\"driver\":\"${QEMU_HOTPLUG_DRIVER}\",\"drive\":\"hotplugdisk\",\"id\":\"hotplugdisk\"}"
if [[ -n "${QEMU_DEVICE_BUS}" ]]; then
    device_add_args="{\"driver\":\"${QEMU_HOTPLUG_DRIVER}\",\"bus\":\"${QEMU_DEVICE_BUS}\",\"drive\":\"hotplugdisk\",\"id\":\"hotplugdisk\"}"
fi

device_resp="$(qmp_send "${QMP_SOCK}" \
    "{\"execute\":\"device_add\",\"arguments\":${device_add_args}}")"
printf '%s\n' "${device_resp}" > "${WORKDIR}/qmp-device-add.json"
if grep -Fq '"error"' "${WORKDIR}/qmp-device-add.json"; then
    echo "Error: device_add failed"
    cat "${WORKDIR}/qmp-device-add.json"
    exit 1
fi

# Wait for the guest to report a successful wipe and then confirm the file is zeroed.
wait_for_log "${SERIAL_LOG}" "DONE" 240

wait "${QEMU_PID}" || true
QEMU_PID=""

sample_hex="$(dd if="${BACKING_FILE}" bs=4096 count=1 status=none | od -An -tx1 -v | tr -d ' \n')"
if ! [[ "${sample_hex}" =~ ^(00)+$ ]]; then
    echo "Error: hotplug backing file was not wiped to zeroes"
    exit 1
fi

echo "Hotplug QEMU integration test passed."
