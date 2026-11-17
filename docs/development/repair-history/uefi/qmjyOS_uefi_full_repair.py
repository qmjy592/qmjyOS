#!/usr/bin/env python3
"""Apply a guarded, backup-first UEFI/QFS hardening pass to a qmjyOS checkout.

Run from the repository root:
    python3 qmjyOS_uefi_full_repair.py --test

The script never uses /tmp, never deletes existing build products, and never
rewrites files outside the explicit allow-list below.
"""
from __future__ import annotations

import argparse
import datetime as _dt
import os
import re
import shutil
import stat
import subprocess
import sys
from pathlib import Path

ROOT = Path.cwd()

LINKER64 = r'''ENTRY(_start64)

/*
 * The UEFI ELF loader maps PT_LOAD segments with 4 KiB pages and enforces
 * page-granular W^X. Keep each permission class in a distinct, page-aligned
 * PT_LOAD segment. The same layout is also consumed by the BIOS binary path.
 */
PHDRS
{
    text   PT_LOAD FLAGS(5); /* R | X */
    rodata PT_LOAD FLAGS(4); /* R */
    data   PT_LOAD FLAGS(6); /* R | W */
}

SECTIONS
{
    . = 0x00200000;
    __kernel64_start = .;

    .text ALIGN(0x1000) :
    {
        __text_start = .;
        KEEP(*(.text.boot))
        *(.text .text.*)
        . = ALIGN(0x1000);
        __text_end = .;
    } :text

    . = ALIGN(0x1000);
    .rodata :
    {
        __rodata_start = .;
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        . = ALIGN(0x1000);
        __rodata_end = .;
    } :rodata

    . = ALIGN(0x1000);
    .data :
    {
        __data_start = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        *(.got .got.*)
        *(.init_array .init_array.*)
        *(.fini_array .fini_array.*)
        . = ALIGN(0x1000);
        __data_end = .;
    } :data

    . = ALIGN(0x1000);
    .bss (NOLOAD) :
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(0x1000);
        __bss_end = .;
    } :data

    __kernel64_end = .;

    /DISCARD/ :
    {
        *(.eh_frame .eh_frame.*)
        *(.comment)
        *(.note .note.*)
        *(.note.GNU-stack)
    }

    ASSERT((ADDR(.text) & 0xFFF) == 0, "kernel .text is not 4 KiB aligned")
    ASSERT((ADDR(.rodata) & 0xFFF) == 0, "kernel .rodata is not 4 KiB aligned")
    ASSERT((ADDR(.data) & 0xFFF) == 0, "kernel .data is not 4 KiB aligned")
    ASSERT((ADDR(.bss) & 0xFFF) == 0, "kernel .bss is not 4 KiB aligned")
}
'''

UEFI_PAGE_TABLE_BUILDER = r"""static int cpu_supports_nx(void)
{
    EFI_UINTN max_leaf = 0x80000000U;
    EFI_UINTN ebx = 0, ecx = 0, edx = 0;

    __asm__ volatile("cpuid"
                     : "+a"(max_leaf), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     :
                     : "memory");
    if (max_leaf < 0x80000001U)
        return 0;

    max_leaf = 0x80000001U;
    __asm__ volatile("cpuid"
                     : "+a"(max_leaf), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     :
                     : "memory");
    return (edx & (1U << 20)) != 0;
}

/*
 * Build a supervisor-only identity map for the first GiB.
 * Non-kernel memory remains RW+NX using 2 MiB pages. Every 2 MiB
 * region touched by the kernel is split into 4 KiB PTEs; ELF segment
 * flags stored by load_kernel_elf() are then applied page by page.
 * This function deliberately does not reference the loader's file buffer:
 * it may already have been released after PT_LOAD segments were copied.
 *
 * Three pages hold PML4/PDPT/PD. A 128 MiB span can intersect at most
 * 65 PDEs, so 68 contiguous pages are sufficient in the worst case.
 */
static EFI_STATUS build_kernel_identity_page_tables(
    EFI_PHYSICAL_ADDRESS root_physical,
    EFI_UINTN kernel_base,
    EFI_UINTN kernel_size)
{
    volatile EFI_UINTN *pml4;
    volatile EFI_UINTN *pdpt;
    volatile EFI_UINTN *pd;
    EFI_UINTN kernel_end;
    EFI_UINTN first_pde;
    EFI_UINTN last_pde;
    EFI_UINTN table_count;
    EFI_UINTN next_table = 3ULL;
    EFI_UINTN i;
    int nx_available;

    if (root_physical == 0 ||
        (root_physical & (EFI_PAGE_SIZE - 1ULL)) != 0 ||
        root_physical >= 0x40000000ULL ||
        EFI_PAGE_TABLE_PAGES > (0x40000000ULL - root_physical) / EFI_PAGE_SIZE ||
        g_kernel_segment_count == 0 || g_kernel_segment_count > 128ULL ||
        kernel_size == 0 || kernel_base < 0x200000ULL ||
        kernel_base >= 0x40000000ULL ||
        kernel_size > EFI_MAX_KERNEL_SPAN ||
        kernel_size > 0x40000000ULL - kernel_base)
        return (EFI_STATUS)0x8000000000000002ULL;

    kernel_end = kernel_base + kernel_size;
    first_pde = kernel_base >> 21;
    last_pde = (kernel_end - 1ULL) >> 21;
    table_count = last_pde - first_pde + 1ULL;
    if (table_count == 0 || table_count > 65ULL ||
        table_count + 3ULL > EFI_PAGE_TABLE_PAGES)
        return (EFI_STATUS)0x8000000000000002ULL;

    nx_available = cpu_supports_nx();
    if (!nx_available) {
        serial_puts("[QBOOT UEFI] CPU lacks NX; refusing a kernel mapping without W^X.\r\n");
        return (EFI_STATUS)0x8000000000000003ULL;
    }

    zero_bytes((void *)(unsigned long long)root_physical,
               EFI_PAGE_TABLE_PAGES * EFI_PAGE_SIZE);
    pml4 = (volatile EFI_UINTN *)(unsigned long long)root_physical;
    pdpt = (volatile EFI_UINTN *)(unsigned long long)(root_physical + EFI_PAGE_SIZE);
    pd = (volatile EFI_UINTN *)(unsigned long long)(root_physical + 2ULL * EFI_PAGE_SIZE);

    pml4[0] = (root_physical + EFI_PAGE_SIZE) | 0x3ULL;
    pdpt[0] = (root_physical + 2ULL * EFI_PAGE_SIZE) | 0x3ULL;

    /* Map every low 1 GiB page supervisor-only and non-executable. */
    for (i = 0; i < 512ULL; ++i) {
        EFI_UINTN physical = i * 0x200000ULL;
        EFI_UINTN nx = 1ULL << 63;
        if (i >= first_pde && i <= last_pde) {
            EFI_UINTN pt_physical = root_physical + next_table * EFI_PAGE_SIZE;
            volatile EFI_UINTN *pt =
                (volatile EFI_UINTN *)(unsigned long long)pt_physical;
            EFI_UINTN page;
            for (page = 0; page < 512ULL; ++page)
                pt[page] = (physical + page * EFI_PAGE_SIZE) | 0x3ULL | nx;
            pd[i] = pt_physical | 0x3ULL;
            ++next_table;
        } else {
            pd[i] = physical | 0x83ULL | nx;
        }
    }

    /* Override kernel leaves with the validated ELF segment permissions. */
    for (i = 0; i < g_kernel_segment_count; ++i) {
        const EFI_KERNEL_SEGMENT *segment = &g_kernel_segments[i];
        EFI_UINTN rounded;
        EFI_UINTN pages;
        EFI_UINTN page_index;
        EFI_UINTN pte_flags = 0x1ULL;
        EFI_UINTN paddr = segment->physical_start;
        EFI_UINTN memsz = segment->memory_size;
        unsigned int flags = segment->flags;

        if (memsz == 0 || paddr < kernel_base || paddr >= kernel_end ||
            memsz > kernel_end - paddr || (flags & ~7U) != 0 ||
            (flags & 4U) == 0 || (flags & 3U) == 3U ||
            (paddr & (EFI_PAGE_SIZE - 1ULL)) != 0 ||
            !add_u64(memsz, EFI_PAGE_SIZE - 1ULL, &rounded))
            return (EFI_STATUS)0x8000000000000002ULL;
        rounded &= ~(EFI_PAGE_SIZE - 1ULL);
        pages = rounded / EFI_PAGE_SIZE;
        if (flags & 2U)
            pte_flags |= 0x2ULL;
        if (!(flags & 1U))
            pte_flags |= (1ULL << 63);

        for (page_index = 0; page_index < pages; ++page_index) {
            EFI_UINTN physical = paddr + page_index * EFI_PAGE_SIZE;
            EFI_UINTN pde_index = physical >> 21;
            EFI_UINTN pte_index = (physical >> 12) & 0x1FFULL;
            EFI_UINTN pt_slot = 3ULL;
            EFI_UINTN n;
            volatile EFI_UINTN *pt;

            if (pde_index < first_pde || pde_index > last_pde)
                return (EFI_STATUS)0x8000000000000002ULL;
            for (n = first_pde; n < pde_index; ++n)
                ++pt_slot;
            pt = (volatile EFI_UINTN *)(unsigned long long)
                (root_physical + pt_slot * EFI_PAGE_SIZE);
            if ((pd[pde_index] & 0x000FFFFFFFFFF000ULL) !=
                (root_physical + pt_slot * EFI_PAGE_SIZE))
                return (EFI_STATUS)0x8000000000000002ULL;
            pt[pte_index] = physical | pte_flags;
        }
    }

    if (next_table != table_count + 3ULL)
        return (EFI_STATUS)0x8000000000000002ULL;

    serial_puts("[QBOOT UEFI] 4 KiB kernel page tables built with W^X permissions\r\n");
    return EFI_SUCCESS;
}
"""

UEFI_ELF_LAYOUT_TEST = r'''#!/usr/bin/env python3
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
'''


def die(message: str) -> None:
    print(f"[QMJYOS REPAIR ABORT] {message}", file=sys.stderr)
    raise SystemExit(2)


def read_text(rel: str) -> str:
    path = ROOT / rel
    if not path.is_file():
        die(f"required file is missing: {rel}")
    try:
        return path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        die(f"file is not UTF-8 text: {rel}")


def write_text(rel: str, content: str, changes: dict[str, str]) -> None:
    old = read_text(rel)
    if old != content:
        changes[rel] = content


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count == 0:
        if new in text:
            return text
        die(f"cannot safely patch {label}: expected source anchor not found")
    if count != 1:
        die(f"cannot safely patch {label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


def patch_efi_loader(text: str) -> str:
    if "read_cr4(void)" not in text:
        die("efi_main.c has no recognizable read_cr4() helper; refusing to guess the ABI")

    # Save validated PT_LOAD metadata so the page-table builder never depends
    # on the EFI file buffer's lifetime.
    if "static EFI_KERNEL_SEGMENT g_kernel_segments[128];" not in text:
        text = replace_once(
            text,
            "} ELF64_PROGRAM_HEADER;",
            """} ELF64_PROGRAM_HEADER;

typedef struct {
    EFI_UINTN physical_start;
    EFI_UINTN memory_size;
    unsigned int flags;
} EFI_KERNEL_SEGMENT;

static EFI_KERNEL_SEGMENT g_kernel_segments[128];
static EFI_UINTN g_kernel_segment_count;""",
            "validated ELF PT_LOAD metadata storage",
        )

    if "g_kernel_segment_count = 0;" not in text:
        load_start = text.find("static EFI_STATUS load_kernel_elf(")
        load_end = text.find("static EFI_STATUS build_kernel_identity_page_tables", load_start)
        if load_start < 0 or load_end < 0:
            die("cannot locate load_kernel_elf() for PT_LOAD metadata reset")
        load_function = text[load_start:load_end]
        reset_anchor = "    EFI_STATUS status;\n\n    if (file == 0 || file_size < sizeof(header))"
        reset_replacement = "    EFI_STATUS status;\n\n    g_kernel_segment_count = 0;\n    if (file == 0 || file_size < sizeof(header))"
        load_function = replace_once(
            load_function, reset_anchor, reset_replacement,
            "PT_LOAD metadata reset",
        )
        text = text[:load_start] + load_function + text[load_end:]

    if "g_kernel_segments[g_kernel_segment_count].physical_start" not in text:
        segment_anchor = "        if (ph.paddr < minimum) minimum = ph.paddr;\n        if (memory_end > maximum) maximum = memory_end;\n        ++load_count;"
        segment_replacement = """        if (g_kernel_segment_count >= 128ULL)
            return 0x8000000000000002ULL;
        g_kernel_segments[g_kernel_segment_count].physical_start = ph.paddr;
        g_kernel_segments[g_kernel_segment_count].memory_size = ph.memsz;
        g_kernel_segments[g_kernel_segment_count].flags = ph.flags;
        ++g_kernel_segment_count;
        if (ph.paddr < minimum) minimum = ph.paddr;
        if (memory_end > maximum) maximum = memory_end;
        ++load_count;"""
        text = replace_once(
            text, segment_anchor, segment_replacement,
            "saved PT_LOAD segment permissions",
        )

    # Enforce ELF PT_LOAD alignment and verify the page-rounded allocation.
    old_align = '''if (ph.align > 1 && (!is_power_of_two(ph.align) ||
            ((ph.vaddr - ph.offset) & (ph.align - 1ULL)) != 0))
            return 0x8000000000000002ULL;'''
    new_align = '''if (ph.align < EFI_PAGE_SIZE || !is_power_of_two(ph.align) ||
            ((ph.vaddr - ph.offset) & (ph.align - 1ULL)) != 0 ||
            ((ph.paddr - ph.offset) & (ph.align - 1ULL)) != 0)
            return 0x8000000000000002ULL;'''
    rounded_block = new_align + '''
        {
            EFI_UINTN rounded_memsz;
            if (!add_u64(ph.memsz, EFI_PAGE_SIZE - 1ULL, &rounded_memsz))
                return 0x8000000000000002ULL;
            rounded_memsz &= ~(EFI_PAGE_SIZE - 1ULL);
            if (rounded_memsz > 0x40000000ULL - ph.paddr)
                return 0x8000000000000002ULL;
        }'''
    if rounded_block not in text:
        if old_align in text:
            text = replace_once(text, old_align, new_align, "ELF PT_LOAD p_align validation")
            text = replace_once(text, new_align, rounded_block, "PT_LOAD page-rounded 1 GiB bound")
        elif new_align not in text:
            die("cannot safely patch ELF PT_LOAD alignment validation")
        else:
            text = replace_once(text, new_align, rounded_block, "PT_LOAD page-rounded 1 GiB bound")

    # The entry must be backed by bytes in an executable segment, not just BSS.
    old_entry = '''if ((ph.flags & 1U) && header.entry >= ph.vaddr && header.entry < memory_end)
            entry_executable = 1;'''
    new_entry = '''/* The entry must point to bytes present in the ELF file, not zero-filled BSS. */
        if ((ph.flags & 1U) && header.entry >= ph.vaddr &&
            header.entry - ph.vaddr < ph.filesz)
            entry_executable = 1;'''
    if new_entry not in text:
        text = replace_once(text, old_entry, new_entry, "ELF entry executable-range validation")

    # Bound the size of the range for which the page-table builder allocates PTs.
    if "EFI_MAX_KERNEL_SPAN" not in text:
        macro_match = re.search(r"(?m)^#define\s+EFI_MAX_KERNEL_FILE\s+[^\n]+$", text)
        if not macro_match:
            die("cannot locate EFI_MAX_KERNEL_FILE to add the bounded kernel-span constant")
        text = text[:macro_match.end()] + "\n#define EFI_MAX_KERNEL_SPAN (128ULL * 1024ULL * 1024ULL)" + text[macro_match.end():]

    if "maximum - minimum > EFI_MAX_KERNEL_SPAN" not in text:
        span_pattern = re.compile(
            r"(if \(load_count == 0 \|\| !entry_executable \|\| maximum <= minimum\)\s*"
            r"return 0x8000000000000002ULL;)"
        )
        text, count = span_pattern.subn(
            r"\1\n    if (maximum - minimum > EFI_MAX_KERNEL_SPAN)\n"
            r"        return 0x8000000000000002ULL;",
            text,
            count=1,
        )
        if count != 1:
            die("cannot safely add the maximum kernel-span bound")

    # Replace the original 3-page RWX huge-page map with a bounded 4 KiB W^X map.
    pages_macro = re.compile(r"(?m)^#define\s+EFI_PAGE_TABLE_PAGES\s+\d+[uUlL]*\s*$")
    pages_match = pages_macro.search(text)
    if not pages_match:
        die("cannot locate EFI_PAGE_TABLE_PAGES definition")
    if pages_match.group(0).strip() != "#define EFI_PAGE_TABLE_PAGES 68U":
        text = text[:pages_match.start()] + "#define EFI_PAGE_TABLE_PAGES 68U" + text[pages_match.end():]

    if "[QBOOT UEFI] 4 KiB kernel page tables built with W^X permissions" not in text:
        builder_pattern = re.compile(
            r"static EFI_STATUS build_kernel_identity_page_tables\(\s*"
            r"EFI_PHYSICAL_ADDRESS root_physical\s*\)\s*\{.*?\n\}\n\n"
            r"(?=static EFI_STATUS allocate_low_pages)",
            re.S,
        )
        text, count = builder_pattern.subn(UEFI_PAGE_TABLE_BUILDER, text, count=1)
        if count != 1:
            die("cannot safely replace the original identity page-table builder")

    # Feed the validated ELF image and exact loaded span into the W^X builder.
    new_call = "build_kernel_identity_page_tables(page_table_root, kernel_base, kernel_size)"
    if new_call not in text:
        call_pattern = re.compile(r"build_kernel_identity_page_tables\(\s*page_table_root(?:\s*,[^;)]*)?\s*\)")
        text, count = call_pattern.subn(new_call, text, count=1)
        if count != 1:
            die("cannot locate the UEFI identity page-table builder call")

    # LA57 must be rejected because this kernel installs a four-level PML4.
    if "LA57 five-level paging is unsupported" not in text:
        anchor = "serial_init();"
        guard = '''serial_init();
    /* This bootstrap constructs four-level paging only. Fail closed on LA57. */
    if ((read_cr4() & (1ULL << 12)) != 0) {
        serial_puts("[QBOOT UEFI] LA57 five-level paging is unsupported by this kernel bootstrap.\\r\\n");
        return (EFI_STATUS)0x8000000000000003ULL; /* EFI_UNSUPPORTED */
    }'''
        text = replace_once(text, anchor, guard, "UEFI LA57 fail-fast guard")

    return text


def patch_entry(text: str) -> str:
    if '#include "qmjy/boot_offsets.h"' not in text:
        first = '.section .text.boot,"ax"'
        if first not in text:
            die("entry64.S lacks its expected .text.boot section")
        text = replace_once(
            text, first,
            '#include "qmjy/boot_offsets.h"\n\n' + first,
            "boot-info offset include",
        )

    if "152(%r12)" in text:
        text = replace_once(
            text, "152(%r12)",
            "QM_BOOT_INFO_PAGE_TABLE_ROOT_OFFSET(%r12)",
            "page-table-root offset use",
        )
    elif "QM_BOOT_INFO_PAGE_TABLE_ROOT_OFFSET(%r12)" not in text:
        die("entry64.S has neither the known BootInfo offset nor its symbolic replacement")

    declaration_guard = re.search(
        r"#ifndef QBOOT_BIOS_ONLY\s+\.extern\s+main_uefi\s+#endif", text
    )
    call_guard = re.search(
        r"#ifndef QBOOT_BIOS_ONLY\s+mov\s+%r12,\s*%rdi\s+call\s+main_uefi\s+#endif",
        text,
    )
    if not declaration_guard:
        text, count = re.subn(
            r"(?m)^\.extern\s+main_uefi\s*$",
            "#ifndef QBOOT_BIOS_ONLY\n.extern main_uefi\n#endif",
            text,
            count=1,
        )
        if count != 1:
            die("cannot safely isolate the main_uefi external declaration")
    if not call_guard:
        text, count = re.subn(
            r"(?m)^([ \t]*)mov\s+%r12,\s*%rdi[ \t]*\n[ \t]*call\s+main_uefi[ \t]*$",
            r"#ifndef QBOOT_BIOS_ONLY\n\1mov %r12, %rdi\n\1call main_uefi\n#endif",
            text,
            count=1,
        )
        if count != 1:
            die("cannot safely isolate the main_uefi call site")
    if not re.search(r"#ifndef QBOOT_BIOS_ONLY\s+\.extern\s+main_uefi\s+#endif", text):
        die("main_uefi declaration is not isolated by QBOOT_BIOS_ONLY")
    if not re.search(
        r"#ifndef QBOOT_BIOS_ONLY\s+\s*mov\s+%r12,\s*%rdi\s+call\s+main_uefi\s+#endif",
        text,
    ):
        die("main_uefi call is not isolated by QBOOT_BIOS_ONLY")

    # NX bits are installed in the new page tables; NXE must therefore be
    # enabled before CR3 is switched, and CR0.WP must protect read-only leaves.
    lower = text.lower()
    cr3_match = re.search(
        r"(?m)^\s*mov\s+%rax,\s*%cr3\s*$",
        lower,
    )
    nxe_match = re.search(
        r"(?m)^\s*wrmsr\s*$",
        lower,
    )
    if "0xc0000080" not in lower or nxe_match is None or cr3_match is None:
        die("UEFI entry lacks a verifiable EFER.NXE-before-kernel-CR3 sequence")
    if nxe_match.start() > cr3_match.start():
        die("UEFI entry switches to kernel CR3 before enabling EFER.NXE")
    wp_sequence = (
        re.search(r"mov\s+%cr0,\s*%rax", lower) is not None
        and re.search(r"or\s+\$\(1\s*<<\s*16\),\s*%rax", lower) is not None
        and re.search(r"mov\s+%rax,\s*%cr0", lower) is not None
    )
    if not wp_sequence:
        die("UEFI entry does not visibly enable CR0.WP for read-only kernel pages")
    return text


def patch_qfs_builder(text: str) -> str:
    old = '''BUILD_ROOT="${QFS_IDE_BUILD_ROOT:-build/qfs-ide}"
FORMAT_DIR="${QFS_FORMAT_DIR:-build/qboot-qfs-format}"
VERIFY_DIR="${QFS_VERIFY_DIR:-build/qboot-qfs-verify}"'''
    new = '''RUN_ID="${QFS_IDE_RUN_ID:-$(date +%Y%m%d-%H%M%S)-$$}"
BUILD_ROOT="${QFS_IDE_BUILD_ROOT:-build/qfs-ide-$RUN_ID}"
FORMAT_DIR="${QFS_FORMAT_DIR:-build/qboot-qfs-format-$RUN_ID}"
VERIFY_DIR="${QFS_VERIFY_DIR:-build/qboot-qfs-verify-$RUN_ID}"'''
    text = replace_once(text, old, new, "collision-free QFS build directories")

    if "EXTRA_CFLAGS=()" not in text:
        old_compile = '''    clang "${CFLAGS[@]}" \\
        -c "$source" \\
        -o "$output"'''
        new_compile = '''    EXTRA_CFLAGS=()
    if [ "$source" = "arch/x86_64/boot/entry64.S" ]; then
        EXTRA_CFLAGS+=(-DQBOOT_BIOS_ONLY)
    fi

    clang "${CFLAGS[@]}" "${EXTRA_CFLAGS[@]}" \\
        -c "$source" \\
        -o "$output"'''
        text = replace_once(text, old_compile, new_compile, "QFS BIOS-only entry compilation")
    elif "-DQBOOT_BIOS_ONLY" not in text:
        die("QFS builder has EXTRA_CFLAGS but does not define QBOOT_BIOS_ONLY")

    text = replace_once(
        text,
        "ld.lld -m elf_x86_64 \\\n        -T linker64.ld \\",
        "ld.lld -m elf_x86_64 -z max-page-size=0x1000 \\\n        -T linker64.ld \\",
        "QFS ELF page-size alignment",
    )
    return text


def patch_qfs_test(text: str) -> str:
    old = 'RUN_ID="${QFS_IDE_RUN_ID:-$(date +%Y%m%d-%H%M%S)}"'
    new = 'RUN_ID="${QFS_IDE_RUN_ID:-$(date +%Y%m%d-%H%M%S)-$$}"'
    return replace_once(text, old, new, "unique QFS test run directory")


def patch_makefile(text: str) -> str:
    if "-z max-page-size=0x1000" not in text:
        old = "LDFLAGS_64 := -m elf_x86_64 -T linker64.ld -nostdlib -static"
        new = "LDFLAGS_64 := -m elf_x86_64 -z max-page-size=0x1000 -T linker64.ld -nostdlib -static"
        text = replace_once(text, old, new, "4 KiB maximum ELF page size")

    # Wire the already-added UEFI BootInfo host test into the normal foundation suite.
    uefi_test_target = "$(HOST_KERNEL_FOUNDATION_DIR)/boot-info-uefi"
    if uefi_test_target not in text:
        list_pattern = r"(?m)^(\$\(HOST_KERNEL_FOUNDATION_DIR\)/boot-info-multiboot)$"
        replacement = r"\1" + " " + chr(92) + "\n" + uefi_test_target
        text, count = re.subn(list_pattern, replacement, text, count=1)
        if count != 1:
            die("cannot safely add the UEFI BootInfo host test to HOST_KERNEL_FOUNDATION_TESTS")

    if uefi_test_target + ":" not in text:
        rule_anchor = """$(HOST_KERNEL_FOUNDATION_DIR)/boot-info-multiboot: tests/kernel/boot_info_multiboot_test.c kernel/core/boot_info.c | $(HOST_KERNEL_FOUNDATION_DIR)
>$(HOST_CC) $(HOST_TEST_CFLAGS) -Iinclude -o $@ $^
"""
        rule_new = rule_anchor + """
$(HOST_KERNEL_FOUNDATION_DIR)/boot-info-uefi: tests/kernel/boot_info_uefi_test.c kernel/core/boot_info.c | $(HOST_KERNEL_FOUNDATION_DIR)
>$(HOST_CC) $(HOST_TEST_CFLAGS) -Iinclude -o $@ $^
"""
        text = replace_once(text, rule_anchor, rule_new, "UEFI BootInfo host-test build rule")

    if "test-qboot-uefi:" in text and "test_uefi_elf_layout.py" not in text:
        anchor = ">bash tools/qboot/test_uefi_boot.sh"
        text = replace_once(
            text, anchor,
            ">python3 tests/boot/test_uefi_elf_layout.py kernel64-uefi.elf\n" + anchor,
            "UEFI ELF layout test integration",
        )
    return text


def validate_boot_contract() -> None:
    h = read_text("include/qmjy/boot.h")
    offsets = read_text("include/qmjy/boot_offsets.h")
    boot_info = read_text("kernel/core/boot_info.c")
    test_src = ROOT / "tests/kernel/boot_info_uefi_test.c"
    if "QM_BOOT_PROTOCOL_MINOR   1U" not in h and "QM_BOOT_PROTOCOL_MINOR 1U" not in h:
        die("boot.h does not advertise BootInfo protocol minor 1")
    if "QM_BOOT_INFO_PAGE_TABLE_ROOT_OFFSET 152" not in offsets:
        die("boot_offsets.h does not define the expected 152-byte page-table-root offset")
    if "page_table_root_physical" not in h:
        die("boot.h lacks page_table_root_physical")
    if "page_table_root_physical" not in boot_info or not re.search(
        r"_Static_assert\s*\(\s*offsetof\s*\(\s*qm_boot_info_t\s*,\s*page_table_root_physical\s*\)\s*==\s*(?:152|QM_BOOT_INFO_PAGE_TABLE_ROOT_OFFSET)",
        boot_info,
    ):
        die("boot_info.c lacks the compile-time BootInfo page-table-root offset assertion")
    if not test_src.is_file():
        die("tests/kernel/boot_info_uefi_test.c is missing")


def validate_makefile_rules(text: str) -> None:
    for marker in ("kernel64-uefi.elf:", "qboot-uefi:", "test-qboot-uefi:"):
        if marker not in text:
            die(f"Makefile is missing required UEFI rule: {marker}")


def backup_and_write(changes: dict[str, str]) -> Path | None:
    if not changes:
        print("[QMJYOS REPAIR] Source files already contain the planned hardening changes.")
        return None
    stamp = _dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    backup = ROOT / "build" / f"uefi-full-repair-backup-{stamp}"
    if backup.exists():
        backup = ROOT / "build" / f"uefi-full-repair-backup-{stamp}-{os.getpid()}"
    backup.mkdir(parents=True, exist_ok=False)

    originals: dict[str, bool] = {}
    prepared: list[tuple[Path, Path]] = []
    for rel in changes:
        src = ROOT / rel
        existed = src.exists()
        originals[rel] = existed
        if existed:
            dst = backup / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)

    try:
        # Stage every replacement first. Preserve executable mode bits on shell scripts.
        for rel, content in changes.items():
            target = ROOT / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            tmp = target.with_name(target.name + f".repair-{os.getpid()}")
            tmp.write_text(content, encoding="utf-8", newline="\n")
            mode = stat.S_IMODE(target.stat().st_mode) if target.exists() else 0o644
            os.chmod(tmp, mode)
            prepared.append((tmp, target))

        for tmp, target in prepared:
            os.replace(tmp, target)
    except Exception:
        # Roll back only files touched by this transaction; unrelated files remain untouched.
        for rel, existed in originals.items():
            target = ROOT / rel
            saved = backup / rel
            try:
                if existed and saved.exists():
                    shutil.copy2(saved, target)
                elif not existed and target.exists():
                    target.unlink()
            except OSError as rollback_error:
                print(f"[QMJYOS REPAIR WARNING] rollback failed for {rel}: {rollback_error}", file=sys.stderr)
        raise
    finally:
        for tmp, _target in prepared:
            try:
                if tmp.exists():
                    tmp.unlink()
            except OSError:
                pass

    print(f"[QMJYOS REPAIR] Backups: {backup.relative_to(ROOT)}")
    for rel in changes:
        print(f"[QMJYOS REPAIR] Updated: {rel}")
    return backup


def run(command: list[str], label: str) -> int:
    print(f"\n========== {label} ==========")
    print("$ " + " ".join(command))
    try:
        proc = subprocess.run(command, cwd=ROOT, check=False)
        code = proc.returncode
    except OSError as exc:
        print(f"[SKIP/FAIL] {exc}", file=sys.stderr)
        return 127
    print(f"[{ 'PASS' if code == 0 else 'FAIL' }] exit={code}")
    return code


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test", action="store_true", help="run BIOS + UEFI + QFS regression tests after patching")
    parser.add_argument("--no-linker-rewrite", action="store_true", help="keep linker64.ld unchanged (not recommended)")
    args = parser.parse_args()

    if not (ROOT / ".git").exists() or not (ROOT / "Makefile").is_file():
        die("run this script from the root of the qmjyOS Git checkout")

    required = [
        "boot/uefi/efi_main.c", "arch/x86_64/boot/entry64.S", "linker64.ld",
        "kernel/core/boot_info.c", "include/qmjy/boot.h", "include/qmjy/boot_offsets.h",
        "tools/qboot/build_qfs_ide_images.sh", "tools/qboot/test_qfs_ide.sh",
        "tools/qboot/test_uefi_boot.sh", "tests/kernel/boot_info_uefi_test.c", "Makefile",
    ]
    for rel in required:
        if not (ROOT / rel).is_file():
            die(f"local UEFI working tree is incomplete; missing {rel}; no files were changed")

    validate_boot_contract()
    changes: dict[str, str] = {}

    # The link script is deliberately rewritten as three page-isolated PT_LOADs.
    # Skip only when the file already has explicit permission PHDRs and page alignment.
    linker = read_text("linker64.ld")
    linker_ok = all(token in linker for token in (
        "PHDRS", "text   PT_LOAD FLAGS(5)", "rodata PT_LOAD FLAGS(4)",
        "data   PT_LOAD FLAGS(6)", "ALIGN(0x1000)",
    ))
    if not args.no_linker_rewrite and not linker_ok:
        write_text("linker64.ld", LINKER64, changes)

    efi = patch_efi_loader(read_text("boot/uefi/efi_main.c"))
    write_text("boot/uefi/efi_main.c", efi, changes)

    entry = patch_entry(read_text("arch/x86_64/boot/entry64.S"))
    write_text("arch/x86_64/boot/entry64.S", entry, changes)

    qfs_builder = patch_qfs_builder(read_text("tools/qboot/build_qfs_ide_images.sh"))
    write_text("tools/qboot/build_qfs_ide_images.sh", qfs_builder, changes)

    qfs_test = patch_qfs_test(read_text("tools/qboot/test_qfs_ide.sh"))
    write_text("tools/qboot/test_qfs_ide.sh", qfs_test, changes)

    makefile = patch_makefile(read_text("Makefile"))
    validate_makefile_rules(makefile)
    write_text("Makefile", makefile, changes)

    layout_path = ROOT / "tests/boot/test_uefi_elf_layout.py"
    if layout_path.exists():
        existing = layout_path.read_text(encoding="utf-8")
        if existing != UEFI_ELF_LAYOUT_TEST:
            tracked = subprocess.run(
                ["git", "ls-files", "--error-unmatch", "tests/boot/test_uefi_elf_layout.py"],
                cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                check=False,
            ).returncode == 0
            if not tracked:
                die("tests/boot/test_uefi_elf_layout.py already exists as an unrelated untracked file; it was not overwritten")
            changes["tests/boot/test_uefi_elf_layout.py"] = UEFI_ELF_LAYOUT_TEST
    else:
        changes["tests/boot/test_uefi_elf_layout.py"] = UEFI_ELF_LAYOUT_TEST

    backup = backup_and_write(changes)
    if backup is None:
        print("[QMJYOS REPAIR] No source rewrite was necessary; all recognized fixes are present.")

    # Make sure the inserted Python test is syntactically valid before any build.
    compile(UEFI_ELF_LAYOUT_TEST, "tests/boot/test_uefi_elf_layout.py", "exec")
    print("[QMJYOS REPAIR PASS] Guarded source patching and contract checks completed.")

    if not args.test:
        print("\nNext command: python3 qmjyOS_uefi_full_repair.py --test")
        return 0

    results = []
    results.append(run([sys.executable, "-c", "from pathlib import Path; p=Path('tests/boot/test_uefi_elf_layout.py'); compile(p.read_text(encoding='utf-8'), str(p), 'exec')"], "UEFI ELF layout test syntax"))
    results.append(run(["bash", "-n", "tools/qboot/build_qfs_ide_images.sh"], "QFS builder shell syntax"))
    results.append(run(["bash", "-n", "tools/qboot/test_qfs_ide.sh"], "QFS runtime test shell syntax"))
    results.append(run(["bash", "-n", "tools/qboot/test_uefi_boot.sh"], "UEFI runtime test shell syntax"))
    results.append(run([sys.executable, "tools/qbuild/module_plan.py", "--check"], "QBuild module-plan validation"))
    results.append(run(["make", "-j2", "all", "kernel64-uefi.elf", "qboot-uefi"], "BIOS + UEFI kernel/image build"))
    results.append(run(["make", "test-kernel-foundation"], "Kernel foundation + UEFI BootInfo host tests"))
    results.append(run(["make", "test-qboot-uefi"], "OVMF UEFI boot test + ELF layout validation"))
    results.append(run(["bash", "tools/qboot/test_qfs_ide.sh"], "Independent QFS IDE BIOS regression"))
    results.append(run(["bash", "tools/qbuild/qbuild", "test", "--arch", "x86_64", "--boot", "bios", "--jobs", "2"], "Complete QBuild BIOS test suite"))
    results.append(run(["bash", "tools/qbuild/qbuild", "test", "--arch", "x86_64", "--boot", "uefi", "--jobs", "2"], "Complete QBuild UEFI test suite"))
    results.append(run(["git", "diff", "--check"], "Final whitespace and patch integrity check"))

    failures = sum(code != 0 for code in results)
    print("\n========== REPAIR TEST SUMMARY ==========")
    print(f"Passed: {len(results) - failures}/{len(results)}; failed: {failures}/{len(results)}")
    if failures:
        print("[QMJYOS REPAIR] Do not commit/release yet. Inspect the first failing test log.")
        return 1
    print("[QMJYOS REPAIR PASS] All requested local regression commands passed.")
    print("[QMJYOS REPAIR INFO] Review git diff --check and git status before committing.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
