#!/usr/bin/env python3
"""Validate the exact ELF64 PT_LOAD contract consumed by QBoot UEFI."""
from __future__ import annotations
import pathlib
import struct
import sys

PAGE = 4096
LIMIT = 0x40000000
PT_LOAD = 1
PF_X, PF_W, PF_R = 1, 2, 4


def fail(message: str) -> None:
    print(f"[UEFI ELF LAYOUT FAIL] {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> int:
    path = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "kernel64-uefi.elf")
    data = path.read_bytes()
    if len(data) < 64 or data[:4] != b"\x7fELF":
        fail("not an ELF file or truncated ELF64 header")
    ident = data[:16]
    if ident[4] != 2 or ident[5] != 1 or ident[6] != 1:
        fail("expected ELFCLASS64, little-endian, current ELF version")
    e_type, e_machine, e_version = struct.unpack_from("<HHI", data, 16)
    if (e_type, e_machine, e_version) != (2, 62, 1):
        fail("expected ET_EXEC for x86_64")
    entry, phoff = struct.unpack_from("<QQ", data, 24)
    ehsize, phentsize, phnum = struct.unpack_from("<HHH", data, 52)
    if ehsize < 64 or phentsize < 56 or phnum == 0 or phnum > 128:
        fail("invalid ELF header/program-header dimensions")
    if phoff > len(data) or phnum > (len(data) - phoff) // phentsize:
        fail("program-header table extends beyond the file")

    segments = []
    executable_entry = False
    for index in range(phnum):
        off = phoff + index * phentsize
        p_type, p_flags, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align = \
            struct.unpack_from("<IIQQQQQQ", data, off)
        if p_type != PT_LOAD:
            continue
        if p_memsz == 0 or p_filesz > p_memsz:
            fail(f"PT_LOAD[{index}] has invalid file/memory sizes")
        if p_offset > len(data) or p_filesz > len(data) - p_offset:
            fail(f"PT_LOAD[{index}] file range exceeds the ELF")
        if p_vaddr != p_paddr:
            fail(f"PT_LOAD[{index}] is not identity-linked (VMA != LMA)")
        if p_paddr < 0x200000 or p_paddr >= LIMIT or p_memsz > LIMIT - p_paddr:
            fail(f"PT_LOAD[{index}] lies outside the supported 2 MiB..1 GiB window")
        if (p_vaddr | p_paddr) & (PAGE - 1):
            fail(f"PT_LOAD[{index}] start is not 4 KiB aligned")
        if p_align < PAGE or (p_align & (p_align - 1)):
            fail(f"PT_LOAD[{index}] p_align must be a power of two >= 4096")
        if ((p_vaddr - p_offset) & (p_align - 1)) != 0:
            fail(f"PT_LOAD[{index}] p_vaddr and p_offset violate p_align")
        if not (p_flags & PF_R):
            fail(f"PT_LOAD[{index}] is not readable")
        if (p_flags & (PF_W | PF_X)) == (PF_W | PF_X):
            fail(f"PT_LOAD[{index}] violates W^X (writable and executable)")
        if p_flags & ~7:
            fail(f"PT_LOAD[{index}] contains unsupported p_flags bits")
        rounded = (p_memsz + PAGE - 1) & ~(PAGE - 1)
        if rounded > LIMIT - p_paddr:
            fail(f"PT_LOAD[{index}] page-rounded allocation crosses the 1 GiB identity-map limit")
        start, end = p_paddr, p_paddr + rounded
        for old_start, old_end, old_idx in segments:
            if start < old_end and old_start < end:
                fail(f"PT_LOAD[{index}] shares pages with PT_LOAD[{old_idx}]")
        segments.append((start, end, index))
        if (p_flags & PF_X) and p_filesz and p_vaddr <= entry < p_vaddr + p_filesz:
            executable_entry = True

    if not segments:
        fail("ELF has no PT_LOAD segments")
    if not executable_entry:
        fail("ELF entry is not inside file-backed executable bytes")
    segments.sort()
    print(f"[UEFI ELF LAYOUT PASS] {path}: {len(segments)} page-aligned, non-overlapping PT_LOAD segments")
    for start, end, index in segments:
        print(f"  PT_LOAD[{index}]: {start:#x}..{end:#x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
