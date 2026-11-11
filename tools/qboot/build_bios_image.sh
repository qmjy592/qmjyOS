#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$ROOT_DIR"

KERNEL_BIN="${KERNEL_BIN:-kernel64.bin}"
BUILD_DIR="${QBOOT_BUILD_DIR:-build/qboot}"
QBOOT_IMAGE="${QBOOT_IMAGE:-$BUILD_DIR/qboot-bios.img}"

for tool in clang ld.lld dd od wc tr cat mkdir dirname; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "[QBOOT BUILD FAIL] Missing tool: $tool" >&2
        exit 1
    }
done

[ -s "$KERNEL_BIN" ] || {
    echo "[QBOOT BUILD FAIL] Kernel binary is missing: $KERNEL_BIN" >&2
    exit 1
}

mkdir -p "$BUILD_DIR" "$(dirname "$QBOOT_IMAGE")"

STAGE2_SECTORS=16
KERNEL_LBA=$((1 + STAGE2_SECTORS))
KERNEL_BYTES=$(wc -c < "$KERNEL_BIN" | tr -d ' ')
KERNEL_SECTORS=$(((KERNEL_BYTES + 511) / 512))

if [ "$KERNEL_SECTORS" -lt 1 ] || [ "$KERNEL_SECTORS" -gt 768 ]; then
    echo "[QBOOT BUILD FAIL] Unsupported kernel size: $KERNEL_BYTES bytes" >&2
    exit 1
fi

cat > "$BUILD_DIR/kernel_layout.inc" <<EOF_LAYOUT
.equ KERNEL_SECTORS, $KERNEL_SECTORS
.equ KERNEL_LBA, $KERNEL_LBA
EOF_LAYOUT

clang --target=i386-unknown-none -m32 -c \
    boot/bios/qboot_stage1.S \
    -o "$BUILD_DIR/stage1.o"

ld.lld -m elf_i386 --image-base=0 -Ttext 0x7c00 \
    --oformat binary \
    -o "$BUILD_DIR/stage1.bin" \
    "$BUILD_DIR/stage1.o"

clang --target=i386-unknown-none -m32 \
    -I "$BUILD_DIR" \
    -c boot/bios/qboot_stage2.S \
    -o "$BUILD_DIR/stage2.o"

ld.lld -m elf_i386 --image-base=0 -Ttext 0x8000 \
    --oformat binary \
    -o "$BUILD_DIR/stage2.bin" \
    "$BUILD_DIR/stage2.o"

STAGE1_BYTES=$(wc -c < "$BUILD_DIR/stage1.bin" | tr -d ' ')
STAGE2_BYTES=$(wc -c < "$BUILD_DIR/stage2.bin" | tr -d ' ')

[ "$STAGE1_BYTES" -eq 512 ] || {
    echo "[QBOOT BUILD FAIL] Stage 1 size is $STAGE1_BYTES, expected 512" >&2
    exit 1
}

[ "$STAGE2_BYTES" -eq 8192 ] || {
    echo "[QBOOT BUILD FAIL] Stage 2 size is $STAGE2_BYTES, expected 8192" >&2
    exit 1
}

BOOT_SIGNATURE=$(od -An -tx1 -j510 -N2 "$BUILD_DIR/stage1.bin" | tr -d ' \n')
[ "$BOOT_SIGNATURE" = "55aa" ] || {
    echo "[QBOOT BUILD FAIL] Invalid boot signature: $BOOT_SIGNATURE" >&2
    exit 1
}

dd if="$KERNEL_BIN" of="$BUILD_DIR/kernel.padded" \
    bs=512 conv=sync status=none

cat "$BUILD_DIR/stage1.bin" \
    "$BUILD_DIR/stage2.bin" \
    "$BUILD_DIR/kernel.padded" \
    > "$QBOOT_IMAGE"

EXPECTED_BYTES=$((512 + 8192 + KERNEL_SECTORS * 512))
ACTUAL_BYTES=$(wc -c < "$QBOOT_IMAGE" | tr -d ' ')

[ "$ACTUAL_BYTES" -eq "$EXPECTED_BYTES" ] || {
    echo "[QBOOT BUILD FAIL] Image size mismatch" >&2
    exit 1
}

echo "[QBOOT BUILD PASS] Stage 1: $STAGE1_BYTES bytes"
echo "[QBOOT BUILD PASS] Stage 2: $STAGE2_BYTES bytes"
echo "[QBOOT BUILD PASS] Kernel: $KERNEL_BYTES bytes ($KERNEL_SECTORS sectors)"
echo "[QBOOT BUILD PASS] Kernel LBA: $KERNEL_LBA"
echo "[QBOOT BUILD PASS] Image: $QBOOT_IMAGE ($ACTUAL_BYTES bytes)"
