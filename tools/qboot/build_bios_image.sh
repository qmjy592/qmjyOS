#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)"
cd "$ROOT_DIR"

for tool in clang ld.lld dd od; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "[QBOOT BUILD FAIL] Missing tool: $tool" >&2
        exit 1
    }
done

[ -s kernel64.bin ] || {
    echo "[QBOOT BUILD FAIL] kernel64.bin is missing" >&2
    exit 1
}

STAGE2_SECTORS=16
KERNEL_LBA=$((1 + STAGE2_SECTORS))
KERNEL_BYTES=$(wc -c < kernel64.bin | tr -d ' ')
KERNEL_SECTORS=$(((KERNEL_BYTES + 511) / 512))

if [ "$KERNEL_SECTORS" -lt 1 ] || [ "$KERNEL_SECTORS" -gt 768 ]; then
    echo "[QBOOT BUILD FAIL] Unsupported kernel size: $KERNEL_BYTES bytes" >&2
    exit 1
fi

mkdir -p build/qboot

cat > build/qboot/kernel_layout.inc <<EOF_LAYOUT
.equ KERNEL_SECTORS, $KERNEL_SECTORS
.equ KERNEL_LBA, $KERNEL_LBA
EOF_LAYOUT

clang --target=i386-unknown-none -m32 -c \
    boot/bios/qboot_stage1.S -o build/qboot/stage1.o

ld.lld -m elf_i386 --image-base=0 -Ttext 0x7c00 \
    --oformat binary -o build/qboot/stage1.bin build/qboot/stage1.o

clang --target=i386-unknown-none -m32 -c \
    boot/bios/qboot_stage2.S -o build/qboot/stage2.o

ld.lld -m elf_i386 --image-base=0 -Ttext 0x8000 \
    --oformat binary -o build/qboot/stage2.bin build/qboot/stage2.o

STAGE1_BYTES=$(wc -c < build/qboot/stage1.bin | tr -d ' ')
STAGE2_BYTES=$(wc -c < build/qboot/stage2.bin | tr -d ' ')

[ "$STAGE1_BYTES" -eq 512 ] || {
    echo "[QBOOT BUILD FAIL] Stage 1 size is $STAGE1_BYTES, expected 512" >&2
    exit 1
}

[ "$STAGE2_BYTES" -eq 8192 ] || {
    echo "[QBOOT BUILD FAIL] Stage 2 size is $STAGE2_BYTES, expected 8192" >&2
    exit 1
}

BOOT_SIGNATURE=$(od -An -tx1 -j510 -N2 build/qboot/stage1.bin | tr -d ' \n')
[ "$BOOT_SIGNATURE" = "55aa" ] || {
    echo "[QBOOT BUILD FAIL] Invalid boot signature: $BOOT_SIGNATURE" >&2
    exit 1
}

dd if=kernel64.bin of=build/qboot/kernel.padded \
    bs=512 conv=sync status=none

cat build/qboot/stage1.bin \
    build/qboot/stage2.bin \
    build/qboot/kernel.padded \
    > build/qboot/qboot-bios.img

EXPECTED_BYTES=$((512 + 8192 + KERNEL_SECTORS * 512))
ACTUAL_BYTES=$(wc -c < build/qboot/qboot-bios.img | tr -d ' ')

[ "$ACTUAL_BYTES" -eq "$EXPECTED_BYTES" ] || {
    echo "[QBOOT BUILD FAIL] Image size mismatch" >&2
    exit 1
}

echo "[QBOOT BUILD PASS] Stage 1: $STAGE1_BYTES bytes"
echo "[QBOOT BUILD PASS] Stage 2: $STAGE2_BYTES bytes"
echo "[QBOOT BUILD PASS] Kernel: $KERNEL_BYTES bytes ($KERNEL_SECTORS sectors)"
echo "[QBOOT BUILD PASS] Kernel LBA: $KERNEL_LBA"
echo "[QBOOT BUILD PASS] Image: build/qboot/qboot-bios.img ($ACTUAL_BYTES bytes)"
