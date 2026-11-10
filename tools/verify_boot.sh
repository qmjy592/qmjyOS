#!/data/data/com.termux/files/usr/bin/bash
set -euo pipefail

fail() {
    echo "[BOOT FAIL] $*" >&2
    exit 1
}

pass() {
    echo "[BOOT PASS] $*"
}

for f in kernel.elf kernel64.elf kernel64.bin; do
    [ -s "$f" ] || fail "$f 不存在或为空"
done

command -v llvm-readelf >/dev/null || fail "缺少 llvm-readelf"
command -v llvm-nm >/dev/null || fail "缺少 llvm-nm"
command -v python3 >/dev/null || fail "缺少 python3"

llvm-readelf -h kernel.elf > ./.qmjyos-elf32.txt
llvm-readelf -h kernel64.elf > ./.qmjyos-elf64.txt
llvm-readelf -S kernel.elf > ./.qmjyos-sections.txt

grep -Eqi 'Entry point address:.*0x10000c' \
    ./.qmjyos-elf32.txt ||
    fail "32 位入口地址不是 0x10000C"

grep -Eqi 'Entry point address:.*0x200000' \
    ./.qmjyos-elf64.txt ||
    fail "64 位入口地址不是 0x200000"

grep -q '\.multiboot' ./.qmjyos-sections.txt ||
    fail "没有独立的 .multiboot 输出段"

python3 - <<'PY'
import struct
from pathlib import Path

data = Path("kernel.elf").read_bytes()
magic_bytes = struct.pack("<I", 0x1BADB002)

pos = data.find(magic_bytes, 0, min(len(data), 8192))

if pos < 0:
    raise SystemExit("[BOOT FAIL] Multiboot 魔数不在 ELF 前 8 KiB")

if pos + 12 > len(data):
    raise SystemExit("[BOOT FAIL] Multiboot 头被截断")

magic, flags, checksum = struct.unpack_from("<III", data, pos)

if (magic + flags + checksum) & 0xffffffff:
    raise SystemExit("[BOOT FAIL] Multiboot 校验和错误")

print(f"[BOOT PASS] Multiboot header offset = 0x{pos:x}")
print(f"[BOOT PASS] Multiboot flags = 0x{flags:x}")
PY

BIN_SIZE="$(wc -c < kernel64.bin | tr -d ' ')"

[ "$BIN_SIZE" -gt 0 ] || fail "64 位内核映像为空"
[ "$BIN_SIZE" -lt 1048576 ] || fail "64 位内核映像异常过大"

llvm-nm -n kernel64.elf > ./.qmjyos-symbols.txt

get_symbol() {
    awk -v name="$1" '$3 == name {print $1; exit}' \
        ./.qmjyos-symbols.txt
}

KSTART="$(get_symbol __kernel64_start)"
BSS_START="$(get_symbol __bss_start)"
BSS_END="$(get_symbol __bss_end)"
KEND="$(get_symbol __kernel64_end)"

[ -n "$KSTART" ] || fail "缺少 __kernel64_start"
[ -n "$BSS_START" ] || fail "缺少 __bss_start"
[ -n "$BSS_END" ] || fail "缺少 __bss_end"
[ -n "$KEND" ] || fail "缺少 __kernel64_end"

python3 - "$BIN_SIZE" "$KSTART" "$BSS_START" "$BSS_END" "$KEND" <<'PY'
import sys

size = int(sys.argv[1])
start, bss_start, bss_end, end = [
    int(x, 16) for x in sys.argv[2:]
]

if start != 0x200000:
    raise SystemExit("[BOOT FAIL] 64 位内核起始地址异常")

if start + size > bss_start:
    raise SystemExit("[BOOT FAIL] 文件映像与 BSS 重叠")

if not (bss_start <= bss_end <= end):
    raise SystemExit("[BOOT FAIL] BSS 地址范围异常")

print("[BOOT PASS] 64 位文件映像与 BSS 没有重叠")
print(f"[BOOT PASS] kernel64.bin = {size} bytes")
print(f"[BOOT PASS] BSS = 0x{bss_start:x} .. 0x{bss_end:x}")
PY

rm -f ./.qmjyos-elf32.txt \
      ./.qmjyos-elf64.txt \
      ./.qmjyos-sections.txt \
      ./.qmjyos-symbols.txt

pass "启动链静态检查全部通过"
