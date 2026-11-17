#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$ROOT_DIR"

CODE_FD="${OVMF_CODE:-${PREFIX:-}/share/edk2/ovmf/OVMF_CODE.fd}"
VARS_TEMPLATE="${OVMF_VARS:-${PREFIX:-}/share/edk2/ovmf/OVMF_VARS.fd}"
QEMU="${QEMU:-qemu-system-x86_64}"
TIMEOUT_SECONDS="${QBOOT_UEFI_TIMEOUT:-30}"
RUN_ID="${QBOOT_UEFI_RUN_ID:-$(date +%Y%m%d-%H%M%S)-$$}"
EVIDENCE_DIR="build/test-evidence/qboot-uefi"
SERIAL_LOG="$EVIDENCE_DIR/serial-$RUN_ID.log"
VARS_COPY="$EVIDENCE_DIR/OVMF_VARS-$RUN_ID.fd"
ESP="build/qboot-uefi/esp"

fail() {
    echo "[QBOOT UEFI TEST FAIL] $*" >&2
    if [ -f "$SERIAL_LOG" ]; then
        echo "===== $SERIAL_LOG =====" >&2
        cat "$SERIAL_LOG" >&2
    fi
    exit 1
}

case "$RUN_ID" in
    ""|*[!A-Za-z0-9._-]*) fail "Invalid run id" ;;
esac
[ -s "$CODE_FD" ] || fail "OVMF_CODE.fd not found: $CODE_FD"
[ -s "$VARS_TEMPLATE" ] || fail "OVMF_VARS.fd not found: $VARS_TEMPLATE"
[ -s "$ESP/EFI/BOOT/BOOTX64.EFI" ] || fail "BOOTX64.EFI is missing"
[ -s "$ESP/EFI/QMJYOS/KERNEL64-UEFI.ELF" ] || fail "kernel ELF is missing from ESP"
command -v "$QEMU" >/dev/null 2>&1 || fail "QEMU not found"
command -v timeout >/dev/null 2>&1 || fail "timeout command not found"
mkdir -p "$EVIDENCE_DIR"
[ ! -e "$SERIAL_LOG" ] && [ ! -e "$VARS_COPY" ] || fail "Evidence file already exists"
cp "$VARS_TEMPLATE" "$VARS_COPY"

echo "[QBOOT UEFI TEST] Starting OVMF x86_64 with headless serial output"
set +e
timeout --signal=TERM --kill-after=5s "${TIMEOUT_SECONDS}s" \
    "$QEMU" \
      -machine q35,accel=tcg \
      -cpu max \
      -m 512M \
      -drive "if=pflash,format=raw,readonly=on,file=$CODE_FD" \
      -drive "if=pflash,format=raw,file=$VARS_COPY" \
      -drive "format=raw,file=fat:rw:$PWD/$ESP" \
      -boot menu=on \
      -net none \
      -display none \
      -monitor none \
      -serial "file:$SERIAL_LOG" \
      -no-reboot \
      -no-shutdown
QEMU_STATUS=$?
set -e
# This kernel intentionally keeps running (scheduler); timeout is expected.
[ "$QEMU_STATUS" -eq 124 ] || fail "Unexpected QEMU exit status: $QEMU_STATUS"

require_marker() {
    grep -Fq "$2" "$1" || fail "Missing marker: $2"
    echo "[QBOOT UEFI TEST PASS] $2"
}
require_marker "$SERIAL_LOG" "[QBOOT UEFI] ELF64 PT_LOAD image loaded and validated"
require_marker "$SERIAL_LOG" "[QBOOT UEFI] ExitBootServices PASS"
require_marker "$SERIAL_LOG" "[QBOOT UEFI] qm_boot_info_t validated"
require_marker "$SERIAL_LOG" "[BOOT] UEFI qm_boot_info_t accepted."
require_marker "$SERIAL_LOG" "Hello, x86_64 kernel!"
require_marker "$SERIAL_LOG" "Physical page allocator: initialized from qm_boot_info_t memory map."
require_marker "$SERIAL_LOG" "[Ring 3] Basic privilege transition: PASS."
require_marker "$SERIAL_LOG" "Starting cooperative Ring 0 scheduler."
require_marker "$SERIAL_LOG" "[task A] running"
require_marker "$SERIAL_LOG" "[task B] running"

echo "[QBOOT UEFI TEST PASS] OVMF -> EFI -> ELF64 kernel -> scheduler verified"
echo "[QBOOT UEFI TEST] Serial evidence: $SERIAL_LOG"
