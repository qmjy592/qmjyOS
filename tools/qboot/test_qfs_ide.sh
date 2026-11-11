#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$ROOT_DIR"

RUN_ID="${QFS_IDE_RUN_ID:-$(date +%Y%m%d-%H%M%S)}"
RUN_ROOT="build/phase12-unified/qfs-ide-${RUN_ID}"

if [ -e "$RUN_ROOT" ]; then
    echo "[QFS IDE TEST FAIL] Run directory already exists: $RUN_ROOT" >&2
    exit 1
fi

QFS_IDE_BUILD_ROOT="$RUN_ROOT/build"
QFS_FORMAT_DIR="$RUN_ROOT/format"
QFS_VERIFY_DIR="$RUN_ROOT/verify"

export QFS_IDE_BUILD_ROOT QFS_FORMAT_DIR QFS_VERIFY_DIR

FORMAT_IMAGE="${QFS_FORMAT_IMAGE:-$QFS_FORMAT_DIR/qboot-bios.img}"
VERIFY_IMAGE="${QFS_VERIFY_IMAGE:-$QFS_VERIFY_DIR/qboot-bios.img}"
TIMEOUT_SECONDS=20

echo "[QFS IDE TEST] Building independent test kernels."
bash tools/qboot/build_qfs_ide_images.sh

for tool in qemu-system-x86_64 timeout mktemp python3 dd sha256sum cut; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "[QFS IDE TEST FAIL] Missing tool: $tool" >&2
        exit 1
    }
done

[ -s "$FORMAT_IMAGE" ] || {
    echo "[QFS IDE TEST FAIL] Format boot image missing: $FORMAT_IMAGE" >&2
    exit 1
}

[ -s "$VERIFY_IMAGE" ] || {
    echo "[QFS IDE TEST FAIL] Verify boot image missing: $VERIFY_IMAGE" >&2
    exit 1
}

TEST_DIR="$RUN_ROOT/runtime"
mkdir -p "$TEST_DIR"
TEST_DISK="$TEST_DIR/qfs-test-disk.img"

FORMAT_SERIAL="$TEST_DIR/format-serial.log"
FORMAT_DEBUG="$TEST_DIR/format-debug.log"
VERIFY_SERIAL="$TEST_DIR/verify-serial.log"
VERIFY_DEBUG="$TEST_DIR/verify-debug.log"

cleanup() {
    rm -rf "$TEST_DIR"
}
trap cleanup EXIT

fail() {
    echo "[QFS IDE TEST FAIL] $*" >&2

    for log in \
        "$FORMAT_DEBUG" "$FORMAT_SERIAL" \
        "$VERIFY_DEBUG" "$VERIFY_SERIAL"; do
        if [ -f "$log" ]; then
            echo "===== $log =====" >&2
            cat "$log" >&2
        fi
    done

    exit 1
}

hash_file() {
    sha256sum "$1" | cut -d ' ' -f 1
}

hash_range() {
    local skip="$1"
    local count="$2"

    dd if="$TEST_DISK" \
        bs=512 skip="$skip" count="$count" status=none |
        sha256sum | cut -d ' ' -f 1
}

echo "[QFS IDE TEST] Creating isolated 16 MiB secondary disk."

python3 - "$TEST_DISK" <<'PY'
import pathlib
import struct
import sys

path = pathlib.Path(sys.argv[1])
disk_bytes = 16 * 1024 * 1024

with path.open("wb") as stream:
    stream.truncate(disk_bytes)

mbr = bytearray(512)
entry = bytearray(16)

entry[0] = 0x00
entry[4] = 0x7F
struct.pack_into("<I", entry, 8, 2048)
struct.pack_into("<I", entry, 12, 2048)

mbr[446:462] = entry
mbr[510:512] = b"\x55\xAA"

with path.open("r+b") as stream:
    stream.seek(0)
    stream.write(mbr)
PY

[ "$(wc -c < "$TEST_DISK" | tr -d ' ')" -eq 16777216 ] ||
    fail "test disk size mismatch"

PREFIX_BEFORE="$(hash_range 0 2048)"
TAIL_BEFORE="$(hash_range 4096 28672)"

FORMAT_IMAGE_BEFORE="$(hash_file "$FORMAT_IMAGE")"
VERIFY_IMAGE_BEFORE="$(hash_file "$VERIFY_IMAGE")"

require_marker() {
    local file="$1"
    local marker="$2"

    grep -Fq "$marker" "$file" ||
        fail "Missing marker: $marker"

    echo "[QFS IDE TEST PASS] $marker"
}

run_qemu_phase() {
    local image="$1"
    local serial_log="$2"
    local debug_log="$3"
    local phase="$4"
    local status

    echo "[QFS IDE TEST] Starting $phase QEMU boot."

    set +e
    timeout --signal=TERM --kill-after=5s "${TIMEOUT_SECONDS}s" \
        qemu-system-x86_64 \
            -machine pc,accel=tcg \
            -cpu max \
            -m 256M \
            -drive "file=$image,format=raw,if=ide,index=0" \
            -drive "file=$TEST_DISK,format=raw,if=ide,index=1" \
            -boot c \
            -display none \
            -serial "file:$serial_log" \
            -monitor none \
            -debugcon "file:$debug_log" \
            -global isa-debugcon.iobase=0xe9 \
            -no-reboot \
            -no-shutdown

    status=$?
    set -e

    [ "$status" -eq 124 ] ||
        fail "$phase QEMU unexpected exit status: $status"

    require_marker "$debug_log" "QBOOT_STAGE2_OK"
    require_marker "$debug_log" "QBOOT_E820_OK"
    require_marker "$debug_log" "QBOOT_KERNEL_LOADED"
    require_marker "$debug_log" "QBOOT_LONG_MODE_HANDOFF"
}

# First independent boot: format, mount, write, read back and flush.
run_qemu_phase \
    "$FORMAT_IMAGE" \
    "$FORMAT_SERIAL" \
    "$FORMAT_DEBUG" \
    "format"

require_marker \
    "$FORMAT_SERIAL" \
    "[QFS IDE FORMAT PASS] Formatted, wrote, read and flushed."

# Verify that no sector outside the designated partition was changed.
PREFIX_AFTER="$(hash_range 0 2048)"
TAIL_AFTER="$(hash_range 4096 28672)"

[ "$PREFIX_BEFORE" = "$PREFIX_AFTER" ] ||
    fail "data before the test partition was modified"

[ "$TAIL_BEFORE" = "$TAIL_AFTER" ] ||
    fail "data after the test partition was modified"

echo "[QFS IDE TEST PASS] Writes stayed inside the designated partition."

DISK_AFTER_FORMAT="$(hash_file "$TEST_DISK")"

[ "$FORMAT_IMAGE_BEFORE" = "$(hash_file "$FORMAT_IMAGE")" ] ||
    fail "format boot image changed during QEMU test"

# Second independent boot: mount and read only; no format or write call.
run_qemu_phase \
    "$VERIFY_IMAGE" \
    "$VERIFY_SERIAL" \
    "$VERIFY_DEBUG" \
    "verify"

require_marker \
    "$VERIFY_SERIAL" \
    "[QFS IDE VERIFY PASS] Data survived independent QEMU restart."

[ "$DISK_AFTER_FORMAT" = "$(hash_file "$TEST_DISK")" ] ||
    fail "verification boot modified the test disk"

[ "$FORMAT_IMAGE_BEFORE" = "$(hash_file "$FORMAT_IMAGE")" ] ||
    fail "format boot image changed during verification boot"

[ "$VERIFY_IMAGE_BEFORE" = "$(hash_file "$VERIFY_IMAGE")" ] ||
    fail "verify boot image changed during QEMU test"

echo "[QFS IDE TEST PASS] Verification boot performed no disk writes."
echo "[QFS IDE TEST PASS] QFS data survived independent QEMU restart."
echo "[QFS IDE TEST PASS] Both QBoot system images remained unchanged."
echo "[QFS IDE TEST PASS] Complete QFS IDE persistence test passed."
