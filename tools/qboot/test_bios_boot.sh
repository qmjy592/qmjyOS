#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$ROOT_DIR"

IMAGE="build/qboot/qboot-bios.img"
SERIAL_LOG="${TMPDIR:-/tmp}/qmjyos-qboot-serial-$$.log"
DEBUG_LOG="${TMPDIR:-/tmp}/qmjyos-qboot-debug-$$.log"
TIMEOUT_SECONDS=20

cleanup() {
    rm -f "$SERIAL_LOG" "$DEBUG_LOG"
}
trap cleanup EXIT

fail() {
    echo "[QBOOT TEST FAIL] $*" >&2
    for log in "$DEBUG_LOG" "$SERIAL_LOG"; do
        if [ -f "$log" ]; then
            echo "===== $log =====" >&2
            cat "$log" >&2
        fi
    done
    exit 1
}

[ -s "$IMAGE" ] || fail "BIOS image not found"
command -v qemu-system-x86_64 >/dev/null 2>&1 ||
    fail "qemu-system-x86_64 not installed"
command -v timeout >/dev/null 2>&1 ||
    fail "timeout not installed"

echo "[QBOOT TEST] Booting from BIOS IDE disk..."

set +e
timeout --signal=TERM --kill-after=5s "${TIMEOUT_SECONDS}s" \
    qemu-system-x86_64 \
        -machine pc,accel=tcg \
        -cpu max \
        -m 256M \
        -drive "file=$IMAGE,format=raw,if=ide" \
        -boot c \
        -display none \
        -serial "file:$SERIAL_LOG" \
        -monitor none \
        -debugcon "file:$DEBUG_LOG" \
        -global isa-debugcon.iobase=0xe9 \
        -no-reboot \
        -no-shutdown

QEMU_STATUS=$?
set -e

if [ "$QEMU_STATUS" -ne 124 ]; then
    fail "Unexpected QEMU exit status: $QEMU_STATUS"
fi

require_marker() {
    local file="$1"
    local marker="$2"

    grep -Fq "$marker" "$file" ||
        fail "Missing marker: $marker"

    echo "[QBOOT TEST PASS] $marker"
}

require_marker "$DEBUG_LOG" "QBOOT_STAGE2_OK"
require_marker "$DEBUG_LOG" "QBOOT_E820_OK"
require_marker "$DEBUG_LOG" "QBOOT_KERNEL_LOADED"
require_marker "$DEBUG_LOG" "QBOOT_LONG_MODE_HANDOFF"

require_marker "$SERIAL_LOG" "Hello, x86_64 kernel!"
require_marker "$SERIAL_LOG" "Physical page allocator: initialized from Multiboot mmap."
require_marker "$SERIAL_LOG" "[Ring 3] Basic privilege transition: PASS."
require_marker "$SERIAL_LOG" "Starting cooperative Ring 0 scheduler."
require_marker "$SERIAL_LOG" "[task A] running"
require_marker "$SERIAL_LOG" "[task B] running"

echo "[QBOOT TEST PASS] BIOS disk boot and kernel runtime verified."
