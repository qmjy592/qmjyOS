#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)"
cd "$ROOT"

# QMJYOS_CI_EVIDENCE_LOG_V1
command -v tee >/dev/null 2>&1 || {
    echo "[SANITIZER FAIL] tee is required to retain the test log." >&2
    exit 1
}
RUN_ID="${HOST_SANITIZER_RUN_ID:-$(date +%Y%m%d-%H%M%S)-$$}"
case "$RUN_ID" in
    ""|*[!A-Za-z0-9._-]*)
        echo "[SANITIZER FAIL] Invalid HOST_SANITIZER_RUN_ID: $RUN_ID" >&2
        exit 1
        ;;
esac
EVIDENCE_DIR="build/test-evidence/host-sanitizers"
mkdir -p "$EVIDENCE_DIR"
LOG_FILE="$EVIDENCE_DIR/run-$RUN_ID.log"
[ ! -e "$LOG_FILE" ] || {
    echo "[SANITIZER FAIL] Refusing to overwrite evidence log: $LOG_FILE" >&2
    exit 1
}
exec > >(tee -a "$LOG_FILE") 2>&1
echo "[SANITIZER] Evidence log: $LOG_FILE"

HOST_CC="${HOST_CC:-clang}"

command -v "$HOST_CC" >/dev/null 2>&1 || {
    echo "[SANITIZER FAIL] Compiler not found: $HOST_CC" >&2
    exit 1
}

echo "===== qmjyOS host tests: ASan + UBSan ====="

export ASAN_OPTIONS="detect_leaks=0:halt_on_error=1"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1"

make -B \
    HOST_CC="$HOST_CC" \
    HOST_TEST_CFLAGS="-std=c11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer" \
    test-memory \
    test-paging \
    test-heap \
    test-vfs \
    test-ramfs \
    test-qfs \
    test-interfaces \
    test-kernel-foundation \
    test-elf64 \
    test-process \
    test-services

echo "[SANITIZER PASS] All host-side test targets passed under ASan and UBSan."
