#!/usr/bin/env bash
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$ROOT"

fail() {
    echo "[QFS IDE BUILD FAIL] $*" >&2
    exit 1
}

for tool in clang ld.lld llvm-objcopy python3 make; do
    command -v "$tool" >/dev/null 2>&1 ||
        fail "Missing tool: $tool"
done

[ -s build/modules.mk ] ||
    fail "build/modules.mk missing; generate the normal module plan first"

[ -s tests/qfs_ide_test_main.c ] ||
    fail "QFS IDE test main source missing"

[ -s tools/qboot/build_bios_image.sh ] ||
    fail "QBoot BIOS builder missing"

BUILD_ROOT="${QFS_IDE_BUILD_ROOT:-build/qfs-ide}"
FORMAT_DIR="${QFS_FORMAT_DIR:-build/qboot-qfs-format}"
VERIFY_DIR="${QFS_VERIFY_DIR:-build/qboot-qfs-verify}"

for directory in "$BUILD_ROOT" "$FORMAT_DIR" "$VERIFY_DIR"; do
    [ ! -e "$directory" ] ||
        fail "Refusing to overwrite existing directory: $directory"
done

python3 tools/qbuild/module_plan.py --check ||
    fail "QBuild module plan check failed"

mkdir -p "$BUILD_ROOT/common"

mapfile -t OBJECTS < <(
python3 - <<'PY'
from pathlib import Path

text = Path("build/modules.mk").read_text(encoding="utf-8")
found = False

for line in text.splitlines():
    if line.startswith("OBJS_64 :="):
        found = True
        for item in line.split(":=", 1)[1].split():
            if item != "kernel/core/main.o":
                print(item)
        break

if not found:
    raise SystemExit("OBJS_64 not found")
PY
)

[ "${#OBJECTS[@]}" -gt 0 ] ||
    fail "No common 64-bit objects found"

CFLAGS_TEXT="$(
    make --no-print-directory --silent \
        --eval='qfs-print-flags:;@printf "%s\n" "$(CFLAGS_64)"' \
        qfs-print-flags
)"

[ -n "$CFLAGS_TEXT" ] || fail "CFLAGS_64 unavailable"

read -r -a CFLAGS <<< "$CFLAGS_TEXT"

COMMON_OBJECTS=()

echo "[QFS IDE BUILD] Compiling isolated common objects."

for object in "${OBJECTS[@]}"; do
    source="${object%.o}.c"

    if [ ! -f "$source" ]; then
        source="${object%.o}.S"
    fi

    [ -f "$source" ] ||
        fail "Source missing for object: $object"

    output="$BUILD_ROOT/common/$object"
    mkdir -p "$(dirname "$output")"

    clang "${CFLAGS[@]}" \
        -c "$source" \
        -o "$output"

    COMMON_OBJECTS+=("$output")
done

build_mode() {
    local mode="$1"
    local name="$2"
    local output_dir="$3"

    local kernel_elf="$BUILD_ROOT/$name/kernel64.elf"
    local kernel_bin="$BUILD_ROOT/$name/kernel64.bin"
    local test_object="$BUILD_ROOT/$name/qfs_ide_test_main.o"

    mkdir -p "$BUILD_ROOT/$name"

    echo "[QFS IDE BUILD] Compiling test mode $mode."

    clang "${CFLAGS[@]}" \
        -DQFS_DISK_TEST_MODE="$mode" \
        -c tests/qfs_ide_test_main.c \
        -o "$test_object"

    ld.lld -m elf_x86_64 \
        -T linker64.ld \
        -nostdlib -static \
        -o "$kernel_elf" \
        "${COMMON_OBJECTS[@]}" \
        "$test_object"

    llvm-objcopy -O binary "$kernel_elf" "$kernel_bin"

    [ -s "$kernel_bin" ] ||
        fail "Kernel binary is empty: $kernel_bin"

    QBOOT_BUILD_DIR="$output_dir" \
    QBOOT_IMAGE="$output_dir/qboot-bios.img" \
    KERNEL_BIN="$kernel_bin" \
        bash tools/qboot/build_bios_image.sh

    [ -s "$output_dir/qboot-bios.img" ] ||
        fail "QBoot image was not generated: $output_dir/qboot-bios.img"

    echo "[QFS IDE BUILD PASS] $name"
}

build_mode 1 format "$FORMAT_DIR"
build_mode 2 verify "$VERIFY_DIR"

echo "[QFS IDE BUILD PASS] Format and verify images generated."
echo "[QFS IDE BUILD PASS] Normal kernel objects were not modified."
