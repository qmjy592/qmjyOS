#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)"
cd "$ROOT"

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
