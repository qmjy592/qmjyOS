#!/usr/bin/env bash
set -euo pipefail

LOG_FILE="${TMPDIR:-/tmp}/qmjyos-qemu-boot-$$.log"
TIMEOUT_SECONDS=20

fail() {
    echo "[QEMU BOOT FAIL] $*" >&2
    if [ -f "$LOG_FILE" ]; then
        echo "----- QEMU serial output -----" >&2
        cat "$LOG_FILE" >&2
        echo "------------------------------" >&2
    fi
    rm -f "$LOG_FILE"
    exit 1
}

trap 'rm -f "$LOG_FILE"' EXIT

for file in kernel.elf kernel64.elf kernel64.bin; do
    [ -s "$file" ] || fail "$file does not exist or is empty"
done

command -v qemu-system-x86_64 >/dev/null 2>&1 ||
    fail "qemu-system-x86_64 is not installed"

command -v timeout >/dev/null 2>&1 ||
    fail "timeout command is not installed"

echo "[QEMU BOOT] Starting qmjyOS x86_64 BIOS kernel..."
echo "[QEMU BOOT] Observation window: ${TIMEOUT_SECONDS}s"

set +e
timeout \
    --signal=TERM \
    --kill-after=5s \
    "${TIMEOUT_SECONDS}s" \
    qemu-system-x86_64 \
        -machine pc,accel=tcg \
        -cpu max \
        -m 256M \
        -kernel kernel.elf \
        -display none \
        -serial "file:${LOG_FILE}" \
        -monitor none \
        -no-reboot \
        -no-shutdown
QEMU_STATUS=$?
set -e

echo "----- QEMU serial output -----"
if [ -f "$LOG_FILE" ]; then
    cat "$LOG_FILE"
else
    fail "QEMU did not create the serial log"
fi
echo "------------------------------"

# The kernel is expected to keep running.
# Exit code 124 means timeout stopped a still-running QEMU process.
if [ "$QEMU_STATUS" -ne 124 ]; then
    fail "QEMU exited unexpectedly with status $QEMU_STATUS"
fi

require_log() {
    local marker="$1"

    if ! grep -Fq "$marker" "$LOG_FILE"; then
        fail "Missing expected serial marker: $marker"
    fi

    echo "[QEMU BOOT PASS] Found: $marker"
}

# Confirm the 64-bit kernel reached its runtime initialization.
require_log "Hello, x86_64 kernel!"

# Confirm the isolated Ring 3 transition returned to Ring 0.
require_log "[Ring 3] Basic privilege transition: PASS."

# Confirm the cooperative scheduler started.
require_log "Starting cooperative Ring 0 scheduler."

# Confirm both tasks actually executed.
require_log "[task A] running"
require_log "[task B] running"

echo "[QEMU BOOT PASS] Kernel remained alive for ${TIMEOUT_SECONDS}s."
echo "[QEMU BOOT PASS] BIOS boot, Ring 3 test and two-task scheduling verified."
