#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)"
cd "$ROOT_DIR"

HOST_CC="${HOST_CC:-clang}"
command -v "$HOST_CC" >/dev/null 2>&1 || {
    echo "[QFS TEST FAIL] Host compiler not found: $HOST_CC" >&2
    exit 1
}

RUN_ID="${QFS_TEST_RUN_ID:-$(date +%Y%m%d-%H%M%S)}"
case "$RUN_ID" in
    ""|.|..|*[!A-Za-z0-9._-]*)
        echo "[QFS TEST FAIL] Invalid QFS_TEST_RUN_ID: $RUN_ID" >&2
        exit 1
        ;;
esac
BUILD_DIR="build/qfs-crash-tests/$RUN_ID"
if [ -e "$BUILD_DIR" ]; then
    echo "[QFS TEST FAIL] Refusing to overwrite: $BUILD_DIR" >&2
    exit 1
fi
mkdir -p "$BUILD_DIR"

# QMJYOS_CI_EVIDENCE_LOG_V1
command -v tee >/dev/null 2>&1 || {
    echo "[QFS TEST FAIL] tee is required to retain the test log." >&2
    exit 1
}
LOG_FILE="$BUILD_DIR/run.log"
exec > >(tee -a "$LOG_FILE") 2>&1
echo "[QFS TEST] Evidence log: $LOG_FILE"

COMMON=( -std=c11 -Wall -Wextra -Werror -I. -Iinclude )

"$HOST_CC" "${COMMON[@]}" \
    -o "$BUILD_DIR/qfs-host-test" \
    tests/filesystem/qfs_host_test.c \
    fs/qfs/qfs.c fs/vfs/vfs.c drivers/block/block.c

"$HOST_CC" "${COMMON[@]}" -DQFS_TESTING \
    -o "$BUILD_DIR/qfs-crash-test" \
    tests/filesystem/qfs_crash_test.c \
    fs/qfs/qfs.c fs/vfs/vfs.c drivers/block/block.c

echo "[QFS TEST] Running existing persistence and consistency tests."
"$BUILD_DIR/qfs-host-test"

echo "[QFS TEST] Running redo-journal power-loss matrix."
"$BUILD_DIR/qfs-crash-test"

echo "[QFS TEST PASS] All QFS host and crash-consistency tests passed."
echo "[QFS TEST] Artifacts retained at: $BUILD_DIR"
