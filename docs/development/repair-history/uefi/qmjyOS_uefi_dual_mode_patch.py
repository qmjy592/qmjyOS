#!/usr/bin/env python3
"""Apply the first end-to-end x86_64 UEFI kernel handoff implementation.

Run from the qmjyOS repository root with: python3 qmjyOS_uefi_dual_mode_patch.py
The script is deliberately non-destructive: it checks expected source markers,
creates one-time backups under build/uefi-phase1-backup, and does not touch the
three unrelated untracked files previously observed in the user's worktree.
"""
from pathlib import Path
import re
import shutil
import sys

ROOT = Path.cwd()
REQUIRED = [
    "include/qmjy/boot.h", "kernel/core/boot_info.c",
    "arch/x86_64/boot/entry64.S", "kernel/core/main.c",
    "Makefile", "tools/qbuild/module_plan.py", "linker64.ld",
    "config/modules.status", "boot/README.md",
    "boot/protocols/README.md", "mm/physical/memory.h",
    "tools/qbuild/qbuild_core.py", "tests/qbuild/test_qbuild_core.py",
    ".github/workflows/c-cpp.yml",
]
missing = [p for p in REQUIRED if not (ROOT / p).is_file()]
if missing:
    print("[UEFI PATCH FAIL] Run this script from qmjyOS root; missing:")
    print("\n".join(missing))
    sys.exit(2)

# Only patch the revision we inspected. Fail closed if important code drifted.
checks = {
    "include/qmjy/boot.h": "#define QM_BOOT_MULTIBOOT_V1_MAGIC",
    "kernel/core/boot_info.c": "qm_boot_info_from_multiboot_v1(",
    "arch/x86_64/boot/entry64.S": ".global _start64",
    "kernel/core/main.c": "/* QM_PHASE2_PHASE3_MAIN: Multiboot compatibility adapter + ordered init. */",
    "tools/qbuild/module_plan.py": "def source_map():",
    "mm/physical/memory.h": "#define MM_MAX_RANGES 128U",
    "linker64.ld": "    . = 2M;",
    "tools/qbuild/qbuild_core.py": "def validate_target(arch: str, boot: str, jobs: int) -> None:",
    "tests/qbuild/test_qbuild_core.py": "def test_cli_compatibility(self):",
    ".github/workflows/c-cpp.yml": "check_rejected_target x86_64 uefi",
}
for rel, marker in checks.items():
    if marker not in (ROOT / rel).read_text(encoding="utf-8"):
        print(f"[UEFI PATCH FAIL] Expected source marker not found: {rel}: {marker}")
        sys.exit(3)

NEW_FILES = [
    "boot/uefi/efi_main.c",
    "tests/kernel/boot_info_uefi_test.c",
    "tools/qboot/test_uefi_boot.sh",
    "include/qmjy/boot_offsets.h",
]
for rel in NEW_FILES:
    if (ROOT / rel).exists():
        print(f"[UEFI PATCH FAIL] Refusing to overwrite existing file: {rel}")
        sys.exit(4)

BACKUP_DIR = ROOT / "build/uefi-phase1-backup"
BACKUP_DIR.mkdir(parents=True, exist_ok=True)
MODIFIED = [
    "include/qmjy/boot.h", "kernel/core/boot_info.c",
    "arch/x86_64/boot/entry64.S", "kernel/core/main.c",
    "Makefile", "tools/qbuild/module_plan.py", "linker64.ld",
    "config/modules.status", "boot/README.md", "boot/protocols/README.md",
    "tools/qbuild/qbuild_core.py", "tests/qbuild/test_qbuild_core.py",
    ".github/workflows/c-cpp.yml",
]
for rel in MODIFIED:
    src = ROOT / rel
    dst = BACKUP_DIR / rel
    dst.parent.mkdir(parents=True, exist_ok=True)
    if not dst.exists():
        shutil.copy2(src, dst)


STAGED = {}
EXECUTABLES = set()

def read(rel):
    if rel in STAGED:
        return STAGED[rel]
    return (ROOT / rel).read_text(encoding="utf-8")


def write(rel, value):
    # Stage every edit in memory. Nothing in the worktree is changed until
    # all source transformations and postconditions have passed.
    STAGED[rel] = value

# ---------------------------------------------------------------------------
# Shared boot protocol: a UEFI memory descriptor input adapter, not a UEFI ABI
# header. Descriptor stride is supplied by GetMemoryMap(), as required.
# ---------------------------------------------------------------------------
rel = "include/qmjy/boot.h"
s = read(rel)
s = s.replace("#define QM_BOOT_PROTOCOL_MINOR   0U",
              "#define QM_BOOT_PROTOCOL_MINOR   1U", 1)
if "page_table_root_physical" not in s:
    old_kernel_tail = "    qm_u64_t kernel_entry_virtual;\n\n    qm_u64_t reserved[8];"
    new_kernel_tail = (
        "    qm_u64_t kernel_entry_virtual;\n\n"
        "    /* v1.1: active kernel page-table root; zero for legacy BIOS bridge. */\n"
        "    qm_u64_t page_table_root_physical;\n\n"
        "    qm_u64_t reserved[8];"
    )
    if old_kernel_tail not in s:
        raise SystemExit("qm_boot_info_t kernel tail changed; refusing layout patch")
    s = s.replace(old_kernel_tail, new_kernel_tail, 1)
insert = r'''
/* UEFI memory descriptor prefix. The firmware descriptor stride may be larger
 * than this structure; consumers must advance by the returned DescriptorSize. */
typedef struct qm_boot_uefi_memory_descriptor {
    qm_u32_t type;
    qm_u32_t reserved;
    qm_u64_t physical_start;
    qm_u64_t virtual_start;
    qm_u64_t number_of_pages;
    qm_u64_t attributes;
} qm_boot_uefi_memory_descriptor_t;

'''
if "qm_boot_uefi_memory_descriptor_t" not in s:
    s = s.replace("#define QM_BOOT_MULTIBOOT_V1_MAGIC", insert + "#define QM_BOOT_MULTIBOOT_V1_MAGIC", 1)
func_decl = r'''
/* Convert the variable-stride UEFI GetMemoryMap() result into the stable
 * qm_boot_memory_region_t array. Kernel pages are explicitly marked reserved
 * as QM_MEMORY_KERNEL. The caller owns both output buffers. */
qm_status_t qm_boot_info_from_uefi(
    const void *memory_map_view,
    qm_u64_t memory_map_size,
    qm_u64_t memory_map_descriptor_size,
    qm_boot_memory_region_t *region_storage,
    qm_u64_t region_storage_physical,
    qm_u32_t region_capacity,
    qm_u64_t kernel_physical_base,
    qm_u64_t kernel_physical_size,
    qm_u64_t kernel_virtual_base,
    qm_u64_t kernel_entry_virtual,
    const qm_boot_framebuffer_t *framebuffer,
    qm_u64_t acpi_rsdp_physical,
    qm_u64_t firmware_system_table_physical,
    qm_u64_t page_table_root_physical,
    qm_boot_info_t *out_info
);
'''
if "qm_boot_info_from_uefi(" not in s:
    s = s.replace("\n#endif\n", "\n" + func_decl + "\n#endif\n", 1)
if "#define QM_BOOT_PROTOCOL_MINOR   1U" not in s or "qm_u64_t page_table_root_physical;" not in s:
    raise SystemExit("boot protocol v1.1 layout extension failed")
write(rel, s)

rel = "linker64.ld"
s = read(rel)
s = s.replace("    .rodata :", "    . = ALIGN(4096);\n    .rodata :", 1)
s = s.replace("    .data :", "    . = ALIGN(4096);\n    .data :", 1)
if "    . = ALIGN(4096);\n    .rodata :" not in s or "    . = ALIGN(4096);\n    .data :" not in s:
    raise SystemExit("linker64.ld section page-alignment insertion failed")
write(rel, s)

write("include/qmjy/boot_offsets.h", r'''#ifndef QMJY_BOOT_OFFSETS_H
#define QMJY_BOOT_OFFSETS_H

/* Must match offsetof(qm_boot_info_t, page_table_root_physical). */
#define QM_BOOT_INFO_PAGE_TABLE_ROOT_OFFSET 152

#endif
''')

# ---------------------------------------------------------------------------
# Common, host-testable UEFI memory map normalization.
# UEFI memory types: LoaderCode/Data=1/2, BootServicesCode/Data=3/4,
# Conventional=7, Unusable=8, ACPIReclaim=9, ACPINVS=10.
# ---------------------------------------------------------------------------
rel = "kernel/core/boot_info.c"
s = read(rel)
s = s.replace(
'''    if (info->firmware != QM_BOOT_FIRMWARE_BIOS &&
        info->firmware != QM_BOOT_FIRMWARE_UEFI &&
        info->firmware != QM_BOOT_FIRMWARE_EFI_STUB)
        return QM_ERR_NOT_SUPPORTED;
''',
'''    if (info->firmware != QM_BOOT_FIRMWARE_BIOS &&
        info->firmware != QM_BOOT_FIRMWARE_UEFI &&
        info->firmware != QM_BOOT_FIRMWARE_EFI_STUB)
        return QM_ERR_NOT_SUPPORTED;

    if (info->protocol_minor >= 1U &&
        info->firmware == QM_BOOT_FIRMWARE_UEFI &&
        (info->page_table_root_physical == 0 ||
         (info->page_table_root_physical & 0xFFFULL) != 0 ||
         info->page_table_root_physical >= 0x40000000ULL))
        return QM_ERR_CORRUPT;
''', 1)
s = s.replace(
'''#include <stdint.h>
''',
'''#include <stdint.h>
#include "qmjy/boot_offsets.h"

_Static_assert(offsetof(qm_boot_info_t, page_table_root_physical) ==
               QM_BOOT_INFO_PAGE_TABLE_ROOT_OFFSET,
               "boot info page-table-root offset changed; update entry64.S");
''', 1)
if "page_table_root_physical == 0" not in s or "_Static_assert(offsetof(qm_boot_info_t, page_table_root_physical)" not in s:
    raise SystemExit("boot_info.c validator or page-table offset assertion insertion failed")
if "qm_boot_info_from_uefi(" not in s:
    s += r'''

#define QM_UEFI_PAGE_SIZE 4096ULL

static qm_u32_t qm_boot_map_uefi_type(qm_u32_t type)
{
    switch (type) {
    case 1: /* EfiLoaderCode */
    case 2: /* EfiLoaderData */
    case 3: /* EfiBootServicesCode */
    case 4: /* EfiBootServicesData */
        return QM_MEMORY_BOOTLOADER_RECLAIM;
    case 7: /* EfiConventionalMemory */
        return QM_MEMORY_USABLE;
    case 8: /* EfiUnusableMemory */
        return QM_MEMORY_BAD;
    case 9: /* EfiACPIReclaimMemory */
        return QM_MEMORY_ACPI_RECLAIM;
    case 10: /* EfiACPIMemoryNVS */
        return QM_MEMORY_ACPI_NVS;
    default:
        return QM_MEMORY_RESERVED;
    }
}

static qm_status_t qm_boot_uefi_append_region(
    qm_boot_memory_region_t *regions,
    qm_u32_t *count,
    qm_u32_t capacity,
    qm_u64_t base,
    qm_u64_t length,
    qm_u32_t type,
    qm_u32_t attributes)
{
    qm_u32_t i;
    qm_u64_t end;

    if (length == 0)
        return QM_OK;
    if (range_overflows(base, length))
        return QM_ERR_OVERFLOW;
    end = base + length;

    /* Reject overlap; merge adjacent equal-type descriptors to control count. */
    for (i = 0; i < *count; ++i) {
        qm_u64_t other_end;
        if (range_overflows(regions[i].base, regions[i].length))
            return QM_ERR_CORRUPT;
        other_end = regions[i].base + regions[i].length;
        if (base < other_end && regions[i].base < end)
            return QM_ERR_CORRUPT;
    }
    if (*count != 0) {
        qm_boot_memory_region_t *last = &regions[*count - 1U];
        if (!range_overflows(last->base, last->length) &&
            last->base + last->length == base &&
            last->type == type && last->attributes == attributes) {
            if (last->length > UINT64_MAX - length)
                return QM_ERR_OVERFLOW;
            last->length += length;
            return QM_OK;
        }
    }
    if (*count >= capacity)
        return QM_ERR_NO_SPACE;
    regions[*count].base = base;
    regions[*count].length = length;
    regions[*count].type = type;
    regions[*count].attributes = attributes;
    ++*count;
    return QM_OK;
}

qm_status_t qm_boot_info_from_uefi(
    const void *memory_map_view,
    qm_u64_t memory_map_size,
    qm_u64_t memory_map_descriptor_size,
    qm_boot_memory_region_t *region_storage,
    qm_u64_t region_storage_physical,
    qm_u32_t region_capacity,
    qm_u64_t kernel_physical_base,
    qm_u64_t kernel_physical_size,
    qm_u64_t kernel_virtual_base,
    qm_u64_t kernel_entry_virtual,
    const qm_boot_framebuffer_t *framebuffer,
    qm_u64_t acpi_rsdp_physical,
    qm_u64_t firmware_system_table_physical,
    qm_u64_t page_table_root_physical,
    qm_boot_info_t *out_info)
{
    const qm_u8_t *map = (const qm_u8_t *)memory_map_view;
    qm_boot_memory_region_t parsed[QM_BOOT_MAX_MEMORY_REGIONS];
    qm_boot_info_t result;
    qm_u64_t kernel_end;
    qm_u64_t covered = 0;
    qm_u64_t offset;
    qm_u32_t count = 0;
    qm_size_t i;

    if (map == NULL || region_storage == NULL || out_info == NULL ||
        memory_map_size == 0 ||
        memory_map_descriptor_size < sizeof(qm_boot_uefi_memory_descriptor_t) ||
        memory_map_size % memory_map_descriptor_size != 0 ||
        region_storage_physical == 0 || region_capacity == 0 ||
        region_capacity > QM_BOOT_MAX_MEMORY_REGIONS)
        return QM_ERR_INVALID;
    if (kernel_physical_size == 0 ||
        range_overflows(kernel_physical_base, kernel_physical_size) ||
        range_overflows(kernel_virtual_base, kernel_physical_size))
        return QM_ERR_OVERFLOW;
    kernel_end = kernel_physical_base + kernel_physical_size;
    if (kernel_entry_virtual < kernel_virtual_base ||
        kernel_entry_virtual >= kernel_virtual_base + kernel_physical_size)
        return QM_ERR_CORRUPT;
    if (region_storage_physical > UINT64_MAX -
        (qm_u64_t)region_capacity * sizeof(*region_storage))
        return QM_ERR_OVERFLOW;

    for (offset = 0; offset < memory_map_size;
         offset += memory_map_descriptor_size) {
        qm_boot_uefi_memory_descriptor_t descriptor;
        qm_u64_t length;
        qm_u64_t end;
        qm_u64_t overlap_start;
        qm_u64_t overlap_end;
        qm_u32_t mapped_type;
        qm_u32_t attributes;
        qm_status_t status;

        for (i = 0; i < sizeof(descriptor); ++i)
            ((qm_u8_t *)&descriptor)[i] = map[offset + i];
        if (descriptor.number_of_pages == 0 ||
            descriptor.number_of_pages > UINT64_MAX / QM_UEFI_PAGE_SIZE)
            return QM_ERR_CORRUPT;
        length = descriptor.number_of_pages * QM_UEFI_PAGE_SIZE;
        if (range_overflows(descriptor.physical_start, length))
            return QM_ERR_OVERFLOW;
        end = descriptor.physical_start + length;
        mapped_type = qm_boot_map_uefi_type(descriptor.type);
        attributes = (qm_u32_t)(descriptor.attributes & 0xFFFFFFFFULL);

        overlap_start = descriptor.physical_start > kernel_physical_base ?
                        descriptor.physical_start : kernel_physical_base;
        overlap_end = end < kernel_end ? end : kernel_end;
        if (overlap_start >= overlap_end) {
            status = qm_boot_uefi_append_region(
                parsed, &count, region_capacity,
                descriptor.physical_start, length, mapped_type, attributes);
            if (status != QM_OK)
                return status;
            continue;
        }

        if (descriptor.physical_start < overlap_start) {
            status = qm_boot_uefi_append_region(
                parsed, &count, region_capacity,
                descriptor.physical_start,
                overlap_start - descriptor.physical_start,
                mapped_type, attributes);
            if (status != QM_OK)
                return status;
        }
        status = qm_boot_uefi_append_region(
            parsed, &count, region_capacity,
            overlap_start, overlap_end - overlap_start,
            QM_MEMORY_KERNEL, attributes);
        if (status != QM_OK)
            return status;
        if (covered > UINT64_MAX - (overlap_end - overlap_start))
            return QM_ERR_OVERFLOW;
        covered += overlap_end - overlap_start;
        if (end > overlap_end) {
            status = qm_boot_uefi_append_region(
                parsed, &count, region_capacity,
                overlap_end, end - overlap_end,
                mapped_type, attributes);
            if (status != QM_OK)
                return status;
        }
    }

    if (count == 0 || covered != kernel_physical_size)
        return QM_ERR_CORRUPT;

    for (i = 0; i < sizeof(result); ++i)
        ((qm_u8_t *)&result)[i] = 0;
    result.magic = QM_BOOT_INFO_MAGIC;
    result.protocol_major = QM_BOOT_PROTOCOL_MAJOR;
    result.protocol_minor = QM_BOOT_PROTOCOL_MINOR;
    result.struct_size = (qm_u32_t)sizeof(result);
    result.architecture = QM_BOOT_ARCH_X86_64;
    result.firmware = QM_BOOT_FIRMWARE_UEFI;
    result.memory_map_physical = region_storage_physical;
    result.memory_map_count = count;
    result.memory_map_entry_size = (qm_u32_t)sizeof(*region_storage);
    result.acpi_rsdp_physical = acpi_rsdp_physical;
    result.firmware_system_table_physical = firmware_system_table_physical;
    result.kernel_physical_base = kernel_physical_base;
    result.kernel_physical_size = kernel_physical_size;
    result.kernel_virtual_base = kernel_virtual_base;
    result.kernel_entry_virtual = kernel_entry_virtual;
    result.page_table_root_physical = page_table_root_physical;
    if (framebuffer != NULL)
        result.framebuffer = *framebuffer;

    if (qm_boot_validate_info(&result) != QM_OK)
        return QM_ERR_CORRUPT;
    for (i = 0; i < count; ++i)
        region_storage[i] = parsed[i];
    *out_info = result;
    return QM_OK;
}
'''
write(rel, s)

# ---------------------------------------------------------------------------
# Add a distinct kernel entry symbol. The BIOS `_start64` stays first and
# remains the raw-image entry; UEFI uses the alternate ELF e_entry.
# ---------------------------------------------------------------------------
rel = "arch/x86_64/boot/entry64.S"
s = read(rel)
uefi_entry = r'''

#include "qmjy/boot_offsets.h"

/*
 * UEFI handoff entry. The loader has exited Boot Services and transfers
 * qm_boot_info_t* in RDI using the x86_64 System V kernel ABI. The loader
 * supplies a 1-GiB identity page-table root in the v1.1 boot record. This
 * routine installs a kernel-owned temporary GDT, switches CR3, clears BSS,
 * installs early exception gates, and enters main_uefi().
 */
.section .text.boot.uefi,"ax"
.align 16
.global _start64_uefi
.type _start64_uefi, @function
.extern main_uefi

_start64_uefi:
    cli
    cld
    mov %rdi, %r12

    # Stop depending on firmware-owned GDT memory before replacing its mappings.
    lgdt uefi_gdt_ptr(%rip)
    pushq $0x08
    leaq .Luefi_kernel_cs(%rip), %rax
    pushq %rax
    lretq
.Luefi_kernel_cs:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss

    # Match the current BIOS bootstrap CPU state. The kernel has not yet
    # implemented SMEP/SMAP/PKE handling; disable these firmware leftovers.
    # Clearing PGE also flushes firmware global TLB entries before CR3 changes.
    mov %cr4, %rax
    test $(1 << 17), %rax
    jz .Luefi_pcide_disabled
    # When PCIDE is active, first reload the current CR3 with PCID=0;
    # Intel requires CR3[11:0]=0 before clearing CR4.PCIDE.
    mov %cr3, %rcx
    and $-4096, %rcx
    mov %rcx, %cr3
.Luefi_pcide_disabled:
    mov %cr4, %rax
    btr $7, %rax
    btr $17, %rax
    btr $20, %rax
    btr $21, %rax
    btr $22, %rax
    mov %rax, %cr4

    # Match the BIOS bootstrap CPU state: enable NXE when supported and WP.
    mov $0x80000000, %eax
    cpuid
    cmp $0x80000001, %eax
    jb .Luefi_nx_done
    mov $0x80000001, %eax
    cpuid
    bt $20, %edx
    jnc .Luefi_nx_done
    mov $0xC0000080, %ecx
    rdmsr
    or $(1 << 11), %eax
    wrmsr
.Luefi_nx_done:
    mov %cr0, %rax
    or $(1 << 16), %rax
    mov %rax, %cr0

    # Move onto the kernel's own mapped stack before replacing the firmware CR3.
    lea stack_top64(%rip), %rsp
    and $-16, %rsp
    xor %rbp, %rbp

    # QBoot Protocol v1.1 carries the root of the kernel's 1-GiB identity map.
    mov QM_BOOT_INFO_PAGE_TABLE_ROOT_OFFSET(%r12), %rax
    test %rax, %rax
    jz 2f
    mov %rax, %cr3

    lea __bss_start(%rip), %rdi
    lea __bss_end(%rip), %rcx
    sub %rdi, %rcx
    xor %eax, %eax
    rep stosb

    xor %edi, %edi
    movw %cs, %di
    call idt_init_with_selector

    mov %r12, %rdi
    call main_uefi

1:
    cli
    hlt
    jmp 1b
2:
    cli
    hlt
    jmp 2b

.size _start64_uefi, .-_start64_uefi

.section .rodata
.align 8
uefi_gdt:
    .quad 0
    .quad 0x00AF9A000000FFFF  # 0x08: kernel 64-bit code
    .quad 0x00CF92000000FFFF  # 0x10: kernel data
uefi_gdt_end:
uefi_gdt_ptr:
    .word uefi_gdt_end - uefi_gdt - 1
    .quad uefi_gdt
'''
if "_start64_uefi:" not in s:
    s += uefi_entry
write(rel, s)

# ---------------------------------------------------------------------------
# UEFI application: load kernel64-uefi.elf from the same boot volume, validate
# and load PT_LOAD at fixed physical addresses, collect GOP/ACPI/GetMemoryMap,
# ExitBootServices with MapKey retry, then transfer RDI to the ELF entry.
# ---------------------------------------------------------------------------
uefi_source = r'''#include "qmjy/boot.h"
#include <stddef.h>

/* UEFI x64 uses the Microsoft x64 calling convention. */
typedef unsigned long long EFI_UINTN;
typedef unsigned long long EFI_STATUS;
typedef unsigned long long EFI_PHYSICAL_ADDRESS;
typedef unsigned short EFI_CHAR16;
typedef void *EFI_HANDLE;
#define EFIAPI __attribute__((ms_abi))
#define EFI_ERROR(Status) (((long long)(Status)) < 0)
#define EFI_SUCCESS 0ULL
#define EFI_BUFFER_TOO_SMALL 0x8000000000000005ULL
#define EFI_INVALID_PARAMETER 2ULL
#define EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL 0x00000001U
#define EFI_FILE_MODE_READ 0x0000000000000001ULL
#define EFI_ALLOCATE_MAX_ADDRESS 1U
#define EFI_ALLOCATE_ADDRESS 2U
#define EFI_LOADER_CODE 1U
#define EFI_LOADER_DATA 2U
#define EFI_PAGE_SIZE 4096ULL
#define EFI_MAX_PHYS 0x3FFFFFFFULL
#define EFI_MEMORY_MAP_PAGES 256ULL /* 1 MiB reserved before GetMemoryMap */
#define EFI_PAGE_TABLE_PAGES 3ULL
#define EFI_MAX_KERNEL_FILE (32ULL * 1024ULL * 1024ULL)

static void outb(unsigned short port, unsigned char value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static unsigned char inb(unsigned short port)
{
    unsigned char value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_init(void)
{
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x80);
    outb(0x3F8, 0x01);
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x03);
    outb(0x3FA, 0xC7);
    outb(0x3FC, 0x0B);
}

static void serial_putc(char c)
{
    unsigned int timeout = 1000000U;
    while (!(inb(0x3FD) & 0x20U) && --timeout)
        __asm__ volatile ("pause");
    if (timeout)
        outb(0x3F8, (unsigned char)c);
}

static void serial_puts(const char *text)
{
    while (*text)
        serial_putc(*text++);
}

static EFI_UINTN read_cr4(void)
{
    EFI_UINTN value;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(value));
    return value;
}

static void serial_put_hex64(EFI_UINTN value)
{
    static const char digits[] = "0123456789ABCDEF";
    int shift;
    serial_puts("0x");
    for (shift = 60; shift >= 0; shift -= 4)
        serial_putc(digits[(value >> shift) & 0xFU]);
}

static void halt_forever(void)
{
    for (;;)
        __asm__ volatile ("cli; hlt" ::: "memory");
}

static void log_status(const char *message, EFI_STATUS status)
{
    serial_puts(message);
    serial_puts(" status=");
    serial_put_hex64(status);
    serial_puts("\r\n");
}

typedef struct {
    unsigned int Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char Data4[8];
} EFI_GUID;

typedef struct {
    EFI_UINTN Signature;
    unsigned int Revision;
    unsigned int HeaderSize;
    unsigned int CRC32;
    unsigned int Reserved;
} EFI_TABLE_HEADER;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_STATUS (EFIAPI *Reset)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, unsigned char);
    EFI_STATUS (EFIAPI *OutputString)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, EFI_CHAR16 *);
};

typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(unsigned int, unsigned int,
                                                 EFI_UINTN, EFI_PHYSICAL_ADDRESS *);
typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(EFI_UINTN *, void *, EFI_UINTN *,
                                                 EFI_UINTN *, unsigned int *);
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(unsigned int, EFI_UINTN, void **);
typedef EFI_STATUS (EFIAPI *EFI_FREE_POOL)(void *);
typedef EFI_STATUS (EFIAPI *EFI_SET_WATCHDOG_TIMER)(EFI_UINTN, EFI_UINTN,
                                                     EFI_UINTN, EFI_CHAR16 *);
typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE, EFI_UINTN);
typedef EFI_STATUS (EFIAPI *EFI_OPEN_PROTOCOL)(EFI_HANDLE, EFI_GUID *, void **,
                                                 EFI_HANDLE, EFI_HANDLE, unsigned int);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(EFI_GUID *, void *, void **);

typedef struct {
    EFI_TABLE_HEADER Hdr;
    void *RaiseTPL;
    void *RestoreTPL;
    EFI_ALLOCATE_PAGES AllocatePages;
    void *FreePages;
    EFI_GET_MEMORY_MAP GetMemoryMap;
    EFI_ALLOCATE_POOL AllocatePool;
    EFI_FREE_POOL FreePool;
    void *CreateEvent;
    void *SetTimer;
    void *WaitForEvent;
    void *SignalEvent;
    void *CloseEvent;
    void *CheckEvent;
    void *InstallProtocolInterface;
    void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    void *HandleProtocol;
    void *Reserved;
    void *RegisterProtocolNotify;
    void *LocateHandle;
    void *LocateDevicePath;
    void *InstallConfigurationTable;
    void *LoadImage;
    void *StartImage;
    void *Exit;
    void *UnloadImage;
    EFI_EXIT_BOOT_SERVICES ExitBootServices;
    void *GetNextMonotonicCount;
    void *Stall;
    EFI_SET_WATCHDOG_TIMER SetWatchdogTimer;
    void *ConnectController;
    void *DisconnectController;
    EFI_OPEN_PROTOCOL OpenProtocol;
    void *CloseProtocol;
    void *OpenProtocolInformation;
    void *ProtocolsPerHandle;
    void *LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL LocateProtocol;
    void *InstallMultipleProtocolInterfaces;
    void *UninstallMultipleProtocolInterfaces;
    void *CalculateCrc32;
    void *CopyMem;
    void *SetMem;
    void *CreateEventEx;
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_GUID VendorGuid;
    void *VendorTable;
} EFI_CONFIGURATION_TABLE;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    EFI_CHAR16 *FirmwareVendor;
    unsigned int FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    void *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    void *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    EFI_UINTN NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE *ConfigurationTable;
} EFI_SYSTEM_TABLE;

typedef struct {
    unsigned int Revision;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_HANDLE DeviceHandle;
    void *FilePath;
    void *Reserved;
    unsigned int LoadOptionsSize;
    void *LoadOptions;
    void *ImageBase;
    EFI_UINTN ImageSize;
    unsigned int ImageCodeType;
    unsigned int ImageDataType;
    void *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
struct EFI_FILE_PROTOCOL {
    EFI_UINTN Revision;
    EFI_STATUS (EFIAPI *Open)(EFI_FILE_PROTOCOL *, EFI_FILE_PROTOCOL **,
                               EFI_CHAR16 *, EFI_UINTN, EFI_UINTN);
    EFI_STATUS (EFIAPI *Close)(EFI_FILE_PROTOCOL *);
    void *Delete;
    EFI_STATUS (EFIAPI *Read)(EFI_FILE_PROTOCOL *, EFI_UINTN *, void *);
    void *Write;
    void *GetPosition;
    void *SetPosition;
    EFI_STATUS (EFIAPI *GetInfo)(EFI_FILE_PROTOCOL *, EFI_GUID *, EFI_UINTN *, void *);
    void *SetInfo;
    void *Flush;
    void *OpenEx;
    void *ReadEx;
    void *WriteEx;
    void *FlushEx;
};

typedef struct {
    EFI_UINTN Revision;
    EFI_STATUS (EFIAPI *OpenVolume)(void *, EFI_FILE_PROTOCOL **);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef struct {
    unsigned short Year;
    unsigned char Month, Day, Hour, Minute, Second, Pad1;
    unsigned int Nanosecond;
    short TimeZone;
    unsigned char Daylight, Pad2;
} EFI_TIME;

typedef struct {
    EFI_UINTN Size;
    EFI_UINTN FileSize;
    EFI_UINTN PhysicalSize;
    EFI_TIME CreateTime;
    EFI_TIME LastAccessTime;
    EFI_TIME ModificationTime;
    EFI_UINTN Attribute;
    EFI_CHAR16 FileName[1];
} EFI_FILE_INFO;

typedef struct {
    unsigned int Version;
    unsigned int HorizontalResolution;
    unsigned int VerticalResolution;
    unsigned int PixelFormat;
    unsigned int PixelInformation[4];
    unsigned int PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    unsigned int MaxMode;
    unsigned int Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    EFI_UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    EFI_UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct {
    void *QueryMode;
    void *SetMode;
    void *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef struct {
    unsigned int Type;
    unsigned int Reserved;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_PHYSICAL_ADDRESS VirtualStart;
    EFI_UINTN NumberOfPages;
    EFI_UINTN Attribute;
} EFI_MEMORY_DESCRIPTOR;

/* Guard the UEFI ABI layouts that this freestanding application dereferences. */
_Static_assert(sizeof(EFI_TABLE_HEADER) == 24U, "UEFI table header size");
_Static_assert(offsetof(EFI_BOOT_SERVICES, AllocatePages) == 40U,
               "UEFI AllocatePages offset");
_Static_assert(offsetof(EFI_BOOT_SERVICES, GetMemoryMap) == 56U,
               "UEFI GetMemoryMap offset");
_Static_assert(offsetof(EFI_BOOT_SERVICES, ExitBootServices) == 232U,
               "UEFI ExitBootServices offset");
_Static_assert(offsetof(EFI_BOOT_SERVICES, OpenProtocol) == 280U,
               "UEFI OpenProtocol offset");
_Static_assert(offsetof(EFI_BOOT_SERVICES, LocateProtocol) == 320U,
               "UEFI LocateProtocol offset");
_Static_assert(offsetof(EFI_BOOT_SERVICES, SetWatchdogTimer) == 256U,
               "UEFI SetWatchdogTimer offset");
_Static_assert(offsetof(EFI_SYSTEM_TABLE, BootServices) == 96U,
               "UEFI SystemTable BootServices offset");
_Static_assert(offsetof(EFI_SYSTEM_TABLE, ConfigurationTable) == 112U,
               "UEFI SystemTable ConfigurationTable offset");
_Static_assert(offsetof(EFI_FILE_PROTOCOL, GetInfo) == 64U,
               "UEFI FileProtocol GetInfo offset");
_Static_assert(sizeof(EFI_MEMORY_DESCRIPTOR) == 40U,
               "UEFI memory descriptor prefix size");

/* The x64 UEFI descriptor prefix is 40 bytes; firmware may use a larger stride. */
typedef struct {
    unsigned char bytes[40];
} EFI_MEMORY_DESCRIPTOR_PREFIX;

typedef struct {
    unsigned char bytes[16];
} EFI_GUID_SIZE_CHECK;

static EFI_GUID g_loaded_image_guid = {
    0x5B1B31A1U, 0x9562U, 0x11D2U,
    {0x8E,0x3F,0x00,0xA0,0xC9,0x69,0x72,0x3B}
};
static EFI_GUID g_simple_fs_guid = {
    0x964E5B22U, 0x6459U, 0x11D2U,
    {0x8E,0x39,0x00,0xA0,0xC9,0x69,0x72,0x3B}
};
static EFI_GUID g_file_info_guid = {
    0x09576E92U, 0x6D3FU, 0x11D2U,
    {0x8E,0x39,0x00,0xA0,0xC9,0x69,0x72,0x3B}
};
static EFI_GUID g_gop_guid = {
    0x9042A9DEU, 0x23DCU, 0x4A38U,
    {0x96,0xFB,0x7A,0xDE,0xD0,0x80,0x51,0x6A}
};
static EFI_GUID g_acpi20_guid = {
    0x8868E871U, 0xE4F1U, 0x11D3U,
    {0xBC,0x22,0x00,0x80,0xC7,0x3C,0x88,0x81}
};
static EFI_GUID g_acpi10_guid = {
    0xEB9D2D30U, 0x2D88U, 0x11D3U,
    {0x9A,0x16,0x00,0x90,0x27,0x3F,0xC1,0x4D}
};

static int guid_equal(const EFI_GUID *a, const EFI_GUID *b)
{
    const unsigned char *x = (const unsigned char *)a;
    const unsigned char *y = (const unsigned char *)b;
    unsigned int i;
    for (i = 0; i < 16U; ++i)
        if (x[i] != y[i]) return 0;
    return 1;
}

/* No UEFI CRT is linked. The compiler may lower aggregate copies to memcpy. */
void *memcpy(void *destination, const void *source, EFI_UINTN size)
{
    volatile unsigned char *d = (volatile unsigned char *)destination;
    const volatile unsigned char *s = (const volatile unsigned char *)source;
    EFI_UINTN i;
    for (i = 0; i < size; ++i) d[i] = s[i];
    return destination;
}

static void *zero_bytes(void *pointer, EFI_UINTN size)
{
    volatile unsigned char *p = (volatile unsigned char *)pointer;
    EFI_UINTN i;
    for (i = 0; i < size; ++i) p[i] = 0;
    return pointer;
}

static void copy_bytes(void *destination, const void *source, EFI_UINTN size)
{
    unsigned char *d = (unsigned char *)destination;
    const unsigned char *s = (const unsigned char *)source;
    EFI_UINTN i;
    for (i = 0; i < size; ++i) d[i] = s[i];
}

static int add_u64(EFI_UINTN a, EFI_UINTN b, EFI_UINTN *out)
{
    if (a > ~0ULL - b) return 0;
    *out = a + b;
    return 1;
}

static int is_power_of_two(EFI_UINTN value)
{
    return value != 0 && (value & (value - 1ULL)) == 0;
}

typedef struct __attribute__((packed)) {
    unsigned char ident[16];
    unsigned short type;
    unsigned short machine;
    unsigned int version;
    EFI_UINTN entry;
    EFI_UINTN phoff;
    EFI_UINTN shoff;
    unsigned int flags;
    unsigned short ehsize;
    unsigned short phentsize;
    unsigned short phnum;
    unsigned short shentsize;
    unsigned short shnum;
    unsigned short shstrndx;
} ELF64_HEADER;

typedef struct __attribute__((packed)) {
    unsigned int type;
    unsigned int flags;
    EFI_UINTN offset;
    EFI_UINTN vaddr;
    EFI_UINTN paddr;
    EFI_UINTN filesz;
    EFI_UINTN memsz;
    EFI_UINTN align;
} ELF64_PROGRAM_HEADER;

static int ranges_overlap(EFI_UINTN a, EFI_UINTN alen,
                          EFI_UINTN b, EFI_UINTN blen)
{
    EFI_UINTN ae, be;
    if (!alen || !blen || !add_u64(a, alen, &ae) || !add_u64(b, blen, &be))
        return 1;
    return a < be && b < ae;
}

static EFI_STATUS read_kernel_file(EFI_BOOT_SERVICES *bs,
                                   EFI_HANDLE image_handle,
                                   unsigned char **out_data,
                                   EFI_UINTN *out_size)
{
    EFI_LOADED_IMAGE_PROTOCOL *loaded = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    EFI_FILE_PROTOCOL *root = 0;
    EFI_FILE_PROTOCOL *file = 0;
    EFI_FILE_INFO *file_info = 0;
    EFI_UINTN info_size = 0;
    EFI_UINTN file_size;
    EFI_UINTN offset;
    unsigned char *buffer = 0;
    EFI_STATUS status;
    static EFI_CHAR16 path[] = {
        '\\','E','F','I','\\','Q','M','J','Y','O','S','\\',
        'K','E','R','N','E','L','6','4','-','U','E','F','I','.','E','L','F',0
    };

    status = bs->OpenProtocol(image_handle, &g_loaded_image_guid,
                              (void **)&loaded, image_handle, 0,
                              EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    if (EFI_ERROR(status) || loaded == 0)
        return EFI_ERROR(status) ? status : 0x8000000000000002ULL;

    status = bs->OpenProtocol(loaded->DeviceHandle, &g_simple_fs_guid,
                              (void **)&fs, image_handle, 0,
                              EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    if (EFI_ERROR(status) || fs == 0)
        return EFI_ERROR(status) ? status : 0x8000000000000002ULL;

    status = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(status) || root == 0)
        return EFI_ERROR(status) ? status : 0x8000000000000002ULL;

    status = root->Open(root, &file, path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status) || file == 0) {
        root->Close(root);
        return EFI_ERROR(status) ? status : 0x8000000000000002ULL;
    }

    status = file->GetInfo(file, &g_file_info_guid, &info_size, 0);
    if (status != EFI_BUFFER_TOO_SMALL || info_size < sizeof(EFI_FILE_INFO)) {
        file->Close(file); root->Close(root);
        return EFI_ERROR(status) ? status : 0x8000000000000002ULL;
    }
    status = bs->AllocatePool(EFI_LOADER_DATA, info_size, (void **)&file_info);
    if (EFI_ERROR(status) || file_info == 0) {
        file->Close(file); root->Close(root);
        return EFI_ERROR(status) ? status : 0x8000000000000009ULL;
    }
    status = file->GetInfo(file, &g_file_info_guid, &info_size, file_info);
    if (EFI_ERROR(status)) {
        bs->FreePool(file_info); file->Close(file); root->Close(root);
        return status;
    }
    file_size = file_info->FileSize;
    bs->FreePool(file_info);
    if (file_size == 0 || file_size > EFI_MAX_KERNEL_FILE) {
        file->Close(file); root->Close(root);
        return 0x8000000000000002ULL;
    }
    status = bs->AllocatePool(EFI_LOADER_DATA, file_size, (void **)&buffer);
    if (EFI_ERROR(status) || buffer == 0) {
        file->Close(file); root->Close(root);
        return EFI_ERROR(status) ? status : 0x8000000000000009ULL;
    }

    offset = 0;
    while (offset < file_size) {
        EFI_UINTN amount = file_size - offset;
        status = file->Read(file, &amount, buffer + offset);
        if (EFI_ERROR(status) || amount == 0) {
            bs->FreePool(buffer); file->Close(file); root->Close(root);
            return EFI_ERROR(status) ? status : 0x8000000000000007ULL;
        }
        offset += amount;
    }
    file->Close(file);
    root->Close(root);

    *out_data = buffer;
    *out_size = file_size;
    return EFI_SUCCESS;
}

static EFI_STATUS load_kernel_elf(EFI_BOOT_SERVICES *bs,
                                  const unsigned char *file,
                                  EFI_UINTN file_size,
                                  EFI_UINTN *kernel_base_out,
                                  EFI_UINTN *kernel_size_out,
                                  EFI_UINTN *entry_out)
{
    ELF64_HEADER header;
    EFI_UINTN table_bytes, table_end;
    EFI_UINTN minimum = ~0ULL;
    EFI_UINTN maximum = 0;
    EFI_UINTN kernel_base;
    EFI_UINTN entry = 0;
    unsigned int load_count = 0;
    int entry_executable = 0;
    unsigned int i, j;
    EFI_STATUS status;

    if (file == 0 || file_size < sizeof(header))
        return 0x8000000000000002ULL;
    copy_bytes(&header, file, sizeof(header));
    if (header.ident[0] != 0x7F || header.ident[1] != 'E' ||
        header.ident[2] != 'L' || header.ident[3] != 'F' ||
        header.ident[4] != 2 || header.ident[5] != 1 ||
        header.ident[6] != 1 || header.type != 2 || header.machine != 62 ||
        header.version != 1 || header.ehsize < sizeof(header) ||
        header.phoff < header.ehsize ||
        header.phentsize < sizeof(ELF64_PROGRAM_HEADER) ||
        header.phnum == 0 || header.phnum > 128)
        return 0x8000000000000002ULL;
    if (header.phnum > (~0ULL / header.phentsize))
        return 0x8000000000000002ULL;
    table_bytes = (EFI_UINTN)header.phentsize * header.phnum;
    if (!add_u64(header.phoff, table_bytes, &table_end) || table_end > file_size)
        return 0x8000000000000002ULL;

    for (i = 0; i < header.phnum; ++i) {
        ELF64_PROGRAM_HEADER ph;
        EFI_UINTN position = header.phoff + (EFI_UINTN)i * header.phentsize;
        EFI_UINTN file_end, memory_end;
        copy_bytes(&ph, file + position, sizeof(ph));
        if (ph.type == 2U || ph.type == 3U || ph.type == 7U)
            return 0x8000000000000002ULL; /* PT_DYNAMIC/INTERP/TLS */
        if (ph.type != 1) continue; /* PT_LOAD only */
        if (ph.memsz == 0 || ph.filesz > ph.memsz ||
            !add_u64(ph.offset, ph.filesz, &file_end) || file_end > file_size ||
            !add_u64(ph.paddr, ph.memsz, &memory_end) || ph.vaddr != ph.paddr ||
            ph.paddr < 0x200000ULL || memory_end > 0x40000000ULL ||
            (ph.paddr & (EFI_PAGE_SIZE - 1ULL)) != 0 ||
            (ph.flags & ~7U) != 0 || (ph.flags & 4U) == 0 ||
            (ph.flags & 3U) == 3U)
            return 0x8000000000000002ULL;
        if (ph.align > 1 && (!is_power_of_two(ph.align) ||
            ((ph.vaddr - ph.offset) & (ph.align - 1ULL)) != 0))
            return 0x8000000000000002ULL;
        if ((ph.flags & 1U) && header.entry >= ph.vaddr && header.entry < memory_end)
            entry_executable = 1;
        for (j = 0; j < i; ++j) {
            ELF64_PROGRAM_HEADER previous;
            EFI_UINTN prev_pos = header.phoff + (EFI_UINTN)j * header.phentsize;
            EFI_UINTN prev_rounded;
            EFI_UINTN this_rounded;
            copy_bytes(&previous, file + prev_pos, sizeof(previous));
            if (previous.type != 1) continue;
            if (!add_u64(previous.memsz, EFI_PAGE_SIZE - 1ULL, &prev_rounded) ||
                !add_u64(ph.memsz, EFI_PAGE_SIZE - 1ULL, &this_rounded))
                return 0x8000000000000002ULL;
            prev_rounded &= ~(EFI_PAGE_SIZE - 1ULL);
            this_rounded &= ~(EFI_PAGE_SIZE - 1ULL);
            if (ranges_overlap(ph.paddr, this_rounded,
                               previous.paddr, prev_rounded))
                return 0x8000000000000002ULL;
        }
        if (ph.paddr < minimum) minimum = ph.paddr;
        if (memory_end > maximum) maximum = memory_end;
        ++load_count;
    }
    if (load_count == 0 || !entry_executable || maximum <= minimum)
        return 0x8000000000000002ULL;

    kernel_base = minimum;
    if (maximum <= kernel_base || maximum > 0x40000000ULL)
        return 0x8000000000000002ULL;

    /* Each PT_LOAD owns complete pages: code is executable; data is writable.
     * linker64.ld page-aligns output segment boundaries to prevent W^X aliasing. */
    for (i = 0; i < header.phnum; ++i) {
        ELF64_PROGRAM_HEADER ph;
        EFI_UINTN position = header.phoff + (EFI_UINTN)i * header.phentsize;
        EFI_UINTN rounded_size;
        EFI_UINTN pages;
        EFI_PHYSICAL_ADDRESS requested;
        copy_bytes(&ph, file + position, sizeof(ph));
        if (ph.type != 1) continue;
        if (!add_u64(ph.memsz, EFI_PAGE_SIZE - 1ULL, &rounded_size))
            return 0x8000000000000002ULL;
        rounded_size &= ~(EFI_PAGE_SIZE - 1ULL);
        pages = rounded_size / EFI_PAGE_SIZE;
        /* Load all segments as writable LoaderData. The kernel replaces
         * firmware page tables after ExitBootServices and applies its own
         * executable policy, avoiding firmware code-page RO faults on copy. */
        requested = ph.paddr;
        status = bs->AllocatePages(EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA,
                                   pages, &requested);
        if (EFI_ERROR(status)) return status;
        if (requested != ph.paddr) return 0x8000000000000002ULL;
        zero_bytes((void *)(unsigned long long)ph.paddr, rounded_size);
        if (ph.filesz != 0)
            copy_bytes((void *)(unsigned long long)ph.paddr,
                       file + ph.offset, ph.filesz);
    }

    entry = header.entry;
    *kernel_base_out = kernel_base;
    *kernel_size_out = maximum - kernel_base;
    *entry_out = entry;
    serial_puts("[QBOOT UEFI] ELF64 PT_LOAD image loaded and validated\r\n");
    return EFI_SUCCESS;
}

static EFI_STATUS build_kernel_identity_page_tables(
    EFI_PHYSICAL_ADDRESS root_physical)
{
    volatile EFI_UINTN *pml4 = (volatile EFI_UINTN *)(unsigned long long)root_physical;
    volatile EFI_UINTN *pdpt = (volatile EFI_UINTN *)(unsigned long long)
        (root_physical + EFI_PAGE_SIZE);
    volatile EFI_UINTN *pd = (volatile EFI_UINTN *)(unsigned long long)
        (root_physical + 2ULL * EFI_PAGE_SIZE);
    EFI_UINTN i;

    if ((root_physical & (EFI_PAGE_SIZE - 1ULL)) != 0 ||
        root_physical == 0 ||
        root_physical + EFI_PAGE_TABLE_PAGES * EFI_PAGE_SIZE > 0x40000000ULL)
        return 0x8000000000000002ULL;

    zero_bytes((void *)(unsigned long long)root_physical,
               EFI_PAGE_TABLE_PAGES * EFI_PAGE_SIZE);
    pml4[0] = root_physical + EFI_PAGE_SIZE | 0x3ULL;
    pdpt[0] = root_physical + 2ULL * EFI_PAGE_SIZE | 0x3ULL;
    for (i = 0; i < 512ULL; ++i)
        pd[i] = (i * 0x200000ULL) | 0x83ULL;
    return EFI_SUCCESS;
}

static EFI_STATUS allocate_low_pages(EFI_BOOT_SERVICES *bs, EFI_UINTN pages,
                                     EFI_PHYSICAL_ADDRESS *address)
{
    EFI_PHYSICAL_ADDRESS maximum = EFI_MAX_PHYS;
    EFI_STATUS status = bs->AllocatePages(EFI_ALLOCATE_MAX_ADDRESS,
                                          EFI_LOADER_DATA, pages, &maximum);
    if (EFI_ERROR(status)) return status;
    if (maximum == 0 || maximum > EFI_MAX_PHYS ||
        maximum + pages * EFI_PAGE_SIZE > 0x40000000ULL)
        return 0x8000000000000002ULL;
    *address = maximum;
    return EFI_SUCCESS;
}

static EFI_UINTN find_acpi_rsdp(EFI_SYSTEM_TABLE *st)
{
    EFI_UINTN i;
    if (st == 0 || st->ConfigurationTable == 0) return 0;
    for (i = 0; i < st->NumberOfTableEntries; ++i) {
        EFI_CONFIGURATION_TABLE *entry = &st->ConfigurationTable[i];
        const unsigned char *p;
        unsigned int j;
        unsigned int sum = 0;
        if (guid_equal(&entry->VendorGuid, &g_acpi20_guid) ||
            guid_equal(&entry->VendorGuid, &g_acpi10_guid)) {
            p = (const unsigned char *)entry->VendorTable;
            if (p == 0 || p[0] != 'R' || p[1] != 'S' || p[2] != 'D' ||
                p[3] != ' ' || p[4] != 'P' || p[5] != 'T' ||
                p[6] != 'R' || p[7] != ' ')
                continue;
            for (j = 0; j < 20U; ++j) sum += p[j];
            if ((sum & 0xFFU) != 0) continue;
            if (p[15] >= 2U) {
                unsigned int length = (unsigned int)p[20] |
                    ((unsigned int)p[21] << 8) |
                    ((unsigned int)p[22] << 16) |
                    ((unsigned int)p[23] << 24);
                if (length < 36U || length > 4096U) continue;
                sum = 0;
                for (j = 0; j < length; ++j) sum += p[j];
                if ((sum & 0xFFU) != 0) continue;
            }
            return (EFI_UINTN)entry->VendorTable;
        }
    }
    return 0;
}

static void collect_framebuffer(EFI_BOOT_SERVICES *bs, qm_boot_framebuffer_t *fb)
{
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    EFI_STATUS status;
    EFI_GUID *guid = &g_gop_guid;
    (void)zero_bytes(fb, sizeof(*fb));
    status = bs->LocateProtocol(guid, 0, (void **)&gop);
    if (EFI_ERROR(status) || gop == 0 || gop->Mode == 0 || gop->Mode->Info == 0)
        return;
    if (gop->Mode->FrameBufferBase == 0 || gop->Mode->FrameBufferSize == 0 ||
        gop->Mode->Info->HorizontalResolution == 0 ||
        gop->Mode->Info->VerticalResolution == 0 ||
        gop->Mode->Info->PixelsPerScanLine == 0 ||
        gop->Mode->Info->PixelFormat > 1U)
        return;
    if ((EFI_UINTN)gop->Mode->Info->PixelsPerScanLine > 0xFFFFFFFFULL / 4ULL)
        return;
    fb->physical_address = gop->Mode->FrameBufferBase;
    fb->size = gop->Mode->FrameBufferSize;
    fb->width = gop->Mode->Info->HorizontalResolution;
    fb->height = gop->Mode->Info->VerticalResolution;
    fb->pitch = gop->Mode->Info->PixelsPerScanLine * 4U;
    fb->bits_per_pixel = 32U;
    fb->pixel_format = (qm_u16_t)gop->Mode->Info->PixelFormat;
}

__attribute__((ms_abi))
EFI_STATUS efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table)
{
    EFI_BOOT_SERVICES *bs;
    unsigned char *kernel_file = 0;
    EFI_UINTN kernel_file_size = 0;
    EFI_UINTN kernel_base = 0, kernel_size = 0, kernel_entry = 0;
    EFI_PHYSICAL_ADDRESS boot_page = 0, map_buffer_pa = 0;
    EFI_PHYSICAL_ADDRESS page_table_root = 0;
    void *map_buffer;
    EFI_UINTN map_capacity = EFI_MEMORY_MAP_PAGES * EFI_PAGE_SIZE;
    EFI_UINTN map_size = 0, map_key = 0, descriptor_size = 0;
    unsigned int descriptor_version = 0;
    EFI_STATUS status;
    unsigned int attempt;
    int exited = 0;
    qm_boot_info_t *boot_info;
    qm_boot_memory_region_t *regions;
    qm_boot_framebuffer_t framebuffer;
    EFI_UINTN rsdp;
    EFI_UINTN regions_offset = (sizeof(qm_boot_info_t) + 7ULL) & ~7ULL;
    typedef void (__attribute__((sysv_abi)) *QM_KERNEL_ENTRY)(qm_boot_info_t *);

    serial_init();
    serial_puts("QBOOT_EFI_SERIAL_START\r\n");
    serial_puts("[QBOOT UEFI] x86_64 firmware application entered\r\n");
    if (system_table == 0 || system_table->BootServices == 0)
        halt_forever();
    bs = system_table->BootServices;

    if (read_cr4() & (1ULL << 12)) {
        serial_puts("[QBOOT UEFI] 5-level paging (LA57) is not supported by this kernel yet\r\n");
        halt_forever();
    }

    status = read_kernel_file(bs, image_handle, &kernel_file, &kernel_file_size);
    if (EFI_ERROR(status)) {
        log_status("[QBOOT UEFI] kernel file read failed", status);
        halt_forever();
    }
    serial_puts("[QBOOT UEFI] kernel ELF file read\r\n");

    status = load_kernel_elf(bs, kernel_file, kernel_file_size,
                             &kernel_base, &kernel_size, &kernel_entry);
    bs->FreePool(kernel_file);
    kernel_file = 0;
    if (EFI_ERROR(status)) {
        log_status("[QBOOT UEFI] kernel ELF load failed", status);
        halt_forever();
    }

    status = allocate_low_pages(bs, 1, &boot_page);
    if (EFI_ERROR(status)) {
        log_status("[QBOOT UEFI] boot-info page allocation failed", status);
        halt_forever();
    }
    status = allocate_low_pages(bs, EFI_MEMORY_MAP_PAGES, &map_buffer_pa);
    if (EFI_ERROR(status)) {
        log_status("[QBOOT UEFI] memory-map buffer allocation failed", status);
        halt_forever();
    }
    status = allocate_low_pages(bs, EFI_PAGE_TABLE_PAGES, &page_table_root);
    if (EFI_ERROR(status)) {
        log_status("[QBOOT UEFI] page-table allocation failed", status);
        halt_forever();
    }
    status = build_kernel_identity_page_tables(page_table_root);
    if (EFI_ERROR(status)) {
        log_status("[QBOOT UEFI] page-table construction failed", status);
        halt_forever();
    }
    serial_puts("[QBOOT UEFI] kernel identity page tables prepared\r\n");

    boot_info = (qm_boot_info_t *)(unsigned long long)boot_page;
    regions = (qm_boot_memory_region_t *)
        ((unsigned char *)(unsigned long long)boot_page + regions_offset);
    if (regions_offset + QM_BOOT_MAX_MEMORY_REGIONS * sizeof(*regions) > EFI_PAGE_SIZE) {
        serial_puts("[QBOOT UEFI] boot-info page layout overflow\r\n");
        halt_forever();
    }
    map_buffer = (void *)(unsigned long long)map_buffer_pa;

    collect_framebuffer(bs, &framebuffer);
    rsdp = find_acpi_rsdp(system_table);
    serial_puts("[QBOOT UEFI] GOP and ACPI discovery complete\r\n");

    /* Disable the firmware watchdog before leaving Boot Services so a long-
     * running kernel is not reset by a still-armed loader watchdog. */
    status = bs->SetWatchdogTimer(0, 0, 0, 0);
    if (EFI_ERROR(status)) {
        log_status("[QBOOT UEFI] watchdog disable failed", status);
        halt_forever();
    }

    /* GetMemoryMap must immediately precede ExitBootServices. On an invalid
     * MapKey retry, only GetMemoryMap and ExitBootServices are called. */
    for (attempt = 0; attempt < 5U; ++attempt) {
        map_size = map_capacity;
        status = bs->GetMemoryMap(&map_size, map_buffer, &map_key,
                                  &descriptor_size, &descriptor_version);
        if (status == EFI_BUFFER_TOO_SMALL) {
            serial_puts("[QBOOT UEFI] memory-map buffer too small\r\n");
            halt_forever();
        }
        if (EFI_ERROR(status)) {
            log_status("[QBOOT UEFI] GetMemoryMap failed", status);
            halt_forever();
        }
        if (descriptor_version != 1U ||
            descriptor_size < sizeof(EFI_MEMORY_DESCRIPTOR) ||
            descriptor_size == 0 || map_size == 0 || map_size > map_capacity) {
            serial_puts("[QBOOT UEFI] invalid firmware memory-map layout\r\n");
            halt_forever();
        }
        status = bs->ExitBootServices(image_handle, map_key);
        if (!EFI_ERROR(status)) {
            exited = 1;
            /* Firmware interrupts/boot services are no longer available. */
            __asm__ volatile ("cli" ::: "memory");
            break;
        }
        if ((status & 0xFFFFULL) != EFI_INVALID_PARAMETER) {
            log_status("[QBOOT UEFI] ExitBootServices failed", status);
            halt_forever();
        }
    }
    if (!exited) {
        serial_puts("[QBOOT UEFI] ExitBootServices retry limit reached\r\n");
        halt_forever();
    }

    /* No Boot Services or protocol methods are called after this point. */
    if (attempt != 0)
        serial_puts("[QBOOT UEFI] ExitBootServices MapKey retry succeeded\r\n");
    serial_puts("[QBOOT UEFI] ExitBootServices PASS\r\n");
    status = qm_boot_info_from_uefi(
        map_buffer, map_size, descriptor_size,
        regions, (qm_u64_t)((unsigned long long)boot_page + regions_offset),
        QM_BOOT_MAX_MEMORY_REGIONS,
        kernel_base, kernel_size, kernel_base, kernel_entry,
        &framebuffer, rsdp, (qm_u64_t)(unsigned long long)system_table,
        (qm_u64_t)(unsigned long long)page_table_root, boot_info);
    if (status != QM_OK) {
        serial_puts("[QBOOT UEFI] boot-info normalization failed code=");
        serial_put_hex64((EFI_UINTN)(unsigned int)(-status));
        serial_puts("\r\n");
        halt_forever();
    }
    serial_puts("[QBOOT UEFI] qm_boot_info_t validated\r\n");
    serial_puts("[QBOOT UEFI] kernel handoff via RDI\r\n");

    ((QM_KERNEL_ENTRY)(unsigned long long)kernel_entry)(boot_info);
    serial_puts("[QBOOT UEFI] kernel entry unexpectedly returned\r\n");
    halt_forever();
    return EFI_SUCCESS;
}
'''
write("boot/uefi/efi_main.c", uefi_source)

# ---------------------------------------------------------------------------
# Refactor kernel entry consumers to accept the shared validated boot record.
# BIOS main(magic,info) remains unchanged as a compatibility adapter.
# ---------------------------------------------------------------------------
rel = "kernel/core/main.c"
s = read(rel)
s = s.replace(
'''    serial_puts("Hello, x86_64 kernel!\\n");
    serial_dump_multiboot_map(g_boot_multiboot_magic, g_boot_multiboot_info_addr);
    return QM_OK;''',
'''    serial_puts("Hello, x86_64 kernel!\\n");
    if (g_kernel_boot_info.firmware == QM_BOOT_FIRMWARE_BIOS)
        serial_dump_multiboot_map(g_boot_multiboot_magic, g_boot_multiboot_info_addr);
    else
        serial_puts("[BOOT] UEFI boot information is active.\\n");
    return QM_OK;''', 1)
s = s.replace(
'''    status = mm_init_from_boot_info(
        &g_kernel_boot_info,
        g_boot_multiboot_info_addr, 88U,
        g_boot_multiboot_mmap_addr, g_boot_multiboot_mmap_length);''',
'''    status = mm_init_from_boot_info(
        &g_kernel_boot_info,
        g_kernel_boot_info.firmware == QM_BOOT_FIRMWARE_BIOS ?
            g_boot_multiboot_info_addr : 0,
        g_kernel_boot_info.firmware == QM_BOOT_FIRMWARE_BIOS ? 88U : 0,
        g_kernel_boot_info.firmware == QM_BOOT_FIRMWARE_BIOS ?
            g_boot_multiboot_mmap_addr : 0,
        g_kernel_boot_info.firmware == QM_BOOT_FIRMWARE_BIOS ?
            g_boot_multiboot_mmap_length : 0);''', 1)
s = s.replace(
'''    serial_puts("Physical page allocator: initialized from Multiboot mmap.\\n");''',
'''    if (g_kernel_boot_info.firmware == QM_BOOT_FIRMWARE_BIOS)
        serial_puts("Physical page allocator: initialized from Multiboot mmap.\\n");
    else
        serial_puts("Physical page allocator: initialized from qm_boot_info_t memory map.\\n");''', 1)
marker = "/* QM_PHASE2_PHASE3_MAIN: Multiboot compatibility adapter + ordered init. */"
idx = s.find(marker)
if idx < 0:
    raise SystemExit("Expected main compatibility marker disappeared")
new_tail = r'''/* Shared post-handoff initialization for BIOS and UEFI. */
static void kernel_run_after_boot_info(void)
{
    qm_status_t status;
    qm_kernel_init_entry_t entries[] = {
        {"early-diagnostics", QM_KERNEL_INIT_EARLY, kernel_init_early},
        {"x86-architecture", QM_KERNEL_INIT_ARCH, kernel_init_arch},
        {"physical-memory-and-heap", QM_KERNEL_INIT_MEMORY, kernel_init_memory},
        {"vfs-and-syscall-core", QM_KERNEL_INIT_CORE, kernel_init_core},
        {"storage-filesystem", QM_KERNEL_INIT_FILESYSTEM, kernel_init_storage}
    };
    qm_size_t i;

    for (i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i) {
        status = qm_kernel_init_register(&entries[i]);
        if (status != QM_OK) {
            serial_puts("[KERNEL] ERROR: init callback registration failed.\n");
            serial_put_hex32((uint32_t)status);
            serial_puts("\n");
            for (;;) __asm__ volatile("cli; hlt" ::: "memory");
        }
    }

    status = qm_kernel_init(&g_kernel_boot_info);
    if (status != QM_OK) {
        serial_puts("[KERNEL] ERROR: ordered initialization registry failed.\n");
        serial_put_hex32((uint32_t)status);
        serial_puts("\n");
        for (;;) __asm__ volatile("cli; hlt" ::: "memory");
    }
    serial_puts("[KERNEL] Ordered initialization registry: PASS.\n");

    if (ring3_prepare_and_run() == 0)
        serial_puts("[Ring 3] Basic privilege transition: PASS.\n");
    else
        serial_puts("[Ring 3] Test did not complete successfully.\n");

    task_init();
    serial_puts("Starting cooperative Ring 0 scheduler.\n");
    __asm__ volatile("sti" ::: "memory");
    task_start();
}

/* BIOS compatibility entry: the existing Multiboot v1 path is preserved. */
void main(uint32_t multiboot_magic, uint32_t multiboot_info_addr)
{
    uint64_t kernel_base;
    uint64_t kernel_end;
    uint32_t mmap_addr;
    uint32_t mmap_length;
    qm_status_t status;

    serial_init();
    g_boot_multiboot_magic = multiboot_magic;
    g_boot_multiboot_info_addr = multiboot_info_addr;

    if (multiboot_info_addr == 0 ||
        (uint64_t)multiboot_info_addr + 88U > (1ULL << 30)) {
        serial_puts("[BOOT] ERROR: Multiboot info pointer is outside the early identity map.\n");
        for (;;) __asm__ volatile("cli; hlt" ::: "memory");
    }
    mmap_length = ((const volatile uint32_t *)(uintptr_t)multiboot_info_addr)[11];
    mmap_addr = ((const volatile uint32_t *)(uintptr_t)multiboot_info_addr)[12];
    g_boot_multiboot_mmap_addr = mmap_addr;
    g_boot_multiboot_mmap_length = mmap_length;
    kernel_base = (uint64_t)(uintptr_t)&__kernel64_start;
    kernel_end = (uint64_t)(uintptr_t)&__kernel64_end;
    if (kernel_end <= kernel_base || mmap_addr == 0 || mmap_length == 0) {
        serial_puts("[BOOT] ERROR: kernel or Multiboot memory-map range is invalid.\n");
        for (;;) __asm__ volatile("cli; hlt" ::: "memory");
    }

    status = qm_boot_info_from_multiboot_v1(
        multiboot_magic,
        (const void *)(uintptr_t)multiboot_info_addr,
        multiboot_info_addr,
        (const void *)(uintptr_t)mmap_addr,
        mmap_addr,
        g_kernel_memory_regions,
        (uint64_t)(uintptr_t)g_kernel_memory_regions,
        QM_BOOT_MAX_MEMORY_REGIONS,
        kernel_base,
        kernel_end - kernel_base,
        kernel_base,
        (uint64_t)(uintptr_t)&_start64,
        &g_kernel_boot_info);
    if (status != QM_OK) {
        serial_puts("[BOOT] ERROR: Multiboot v1 to qm_boot_info_t conversion failed.\n");
        serial_put_hex32((uint32_t)status);
        serial_puts("\n");
        for (;;) __asm__ volatile("cli; hlt" ::: "memory");
    }
    serial_puts("[BOOT] Versioned qm_boot_info_t constructed and validated.\n");
    kernel_run_after_boot_info();
}

/* UEFI x64 entry: RDI points to a QBoot Protocol v1 structure. */
void main_uefi(qm_boot_info_t *boot_info)
{
    serial_init();
    g_boot_multiboot_magic = 0;
    g_boot_multiboot_info_addr = 0;
    g_boot_multiboot_mmap_addr = 0;
    g_boot_multiboot_mmap_length = 0;

    if (boot_info == NULL || qm_boot_validate_info(boot_info) != QM_OK ||
        boot_info->architecture != QM_BOOT_ARCH_X86_64 ||
        boot_info->firmware != QM_BOOT_FIRMWARE_UEFI) {
        serial_puts("[BOOT] ERROR: invalid UEFI qm_boot_info_t handoff.\n");
        for (;;) __asm__ volatile("cli; hlt" ::: "memory");
    }
    g_kernel_boot_info = *boot_info;
    serial_puts("[BOOT] UEFI qm_boot_info_t accepted.\n");
    kernel_run_after_boot_info();
}
'''
s = s[:idx] + new_tail
write(rel, s)

# ---------------------------------------------------------------------------
# Host regression test for UEFI descriptor conversion.
# ---------------------------------------------------------------------------
test_source = r'''#include "qmjy/boot.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void init_desc(qm_boot_uefi_memory_descriptor_t *d,
                      uint32_t type, uint64_t base, uint64_t pages)
{
    d->type = type;
    d->reserved = 0;
    d->physical_start = base;
    d->virtual_start = 0;
    d->number_of_pages = pages;
    d->attributes = 0x8ULL;
}

int main(void)
{
    qm_boot_uefi_memory_descriptor_t map[6];
    qm_boot_memory_region_t regions[16];
    qm_boot_memory_region_t sentinel[2];
    qm_boot_info_t info;
    qm_boot_framebuffer_t fb = {0};
    uint32_t i;

    init_desc(&map[0], 0, 0x00000000ULL, 0x100ULL);       /* reserved 0-1 MiB */
    init_desc(&map[1], 7, 0x00100000ULL, 0x100ULL);       /* conventional 1-2 MiB */
    init_desc(&map[2], 2, 0x00200000ULL, 0x100ULL);       /* loader data / kernel */
    init_desc(&map[3], 7, 0x00300000ULL, 0x3D00ULL);      /* conventional to 64 MiB */
    init_desc(&map[4], 9, 0x04000000ULL, 0x100ULL);       /* ACPI reclaim */
    init_desc(&map[5], 10, 0x04100000ULL, 0x100ULL);      /* ACPI NVS */

    assert(qm_boot_info_from_uefi(
        map, sizeof(map), sizeof(map[0]), regions, 0x90000ULL, 16,
        0x200000ULL, 0x100000ULL, 0x200000ULL, 0x200100ULL,
        &fb, 0x04000000ULL, 0xF0000ULL, 0xA0000ULL, &info) == QM_OK);
    assert(info.firmware == QM_BOOT_FIRMWARE_UEFI);
    assert(info.page_table_root_physical == 0xA0000ULL);
    assert(info.architecture == QM_BOOT_ARCH_X86_64);
    assert(info.memory_map_physical == 0x90000ULL);
    assert(qm_boot_validate_info(&info) == QM_OK);

    for (i = 0; i < info.memory_map_count; ++i) {
        if (regions[i].base == 0x00200000ULL) {
            assert(regions[i].length == 0x100000ULL);
            assert(regions[i].type == QM_MEMORY_KERNEL);
        }
    }
    assert(info.memory_map_count >= 5U);

    sentinel[0].base = 0xDEADBEEFULL;
    sentinel[0].length = 0x1000ULL;
    assert(qm_boot_info_from_uefi(
        map, sizeof(map), sizeof(map[0]), sentinel, 0x90000ULL, 1,
        0x200000ULL, 0x100000ULL, 0x200000ULL, 0x200100ULL,
        &fb, 0, 0, 0xA0000ULL, &info) == QM_ERR_NO_SPACE);
    assert(sentinel[0].base == 0xDEADBEEFULL);

    assert(qm_boot_info_from_uefi(
        map, sizeof(map), 16U, regions, 0x90000ULL, 16,
        0x200000ULL, 0x100000ULL, 0x200000ULL, 0x200100ULL,
        &fb, 0, 0, 0xA0000ULL, &info) == QM_ERR_INVALID);

    map[2].number_of_pages = UINT64_MAX;
    assert(qm_boot_info_from_uefi(
        map, sizeof(map), sizeof(map[0]), regions, 0x90000ULL, 16,
        0x200000ULL, 0x100000ULL, 0x200000ULL, 0x200100ULL,
        &fb, 0, 0, 0xA0000ULL, &info) == QM_ERR_CORRUPT);

    puts("UEFI memory-map to QBoot protocol tests: PASS");
    return 0;
}
'''
write("tests/kernel/boot_info_uefi_test.c", test_source)

# ---------------------------------------------------------------------------
# QEMU OVMF headless runtime test. No VNC or graphical client is required.
# ---------------------------------------------------------------------------
test_script = r'''#!/usr/bin/env bash
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
'''
write("tools/qboot/test_uefi_boot.sh", test_script)
EXECUTABLES.add("tools/qboot/test_uefi_boot.sh")

# ---------------------------------------------------------------------------
# Update Makefile: dedicated EFI PE build, alternate ELF entry, and test target.
# ---------------------------------------------------------------------------
rel = "Makefile"
s = read(rel)
if "qboot-uefi:" not in s:
    s += r'''

# QBoot x86_64 UEFI application and UEFI-entry kernel image.
UEFI_BUILD_DIR := build/qboot-uefi
UEFI_ESP_DIR := $(UEFI_BUILD_DIR)/esp
UEFI_CC ?= clang
UEFI_LD ?= lld-link
UEFI_TARGET := --target=x86_64-pc-windows-msvc
UEFI_CFLAGS := $(UEFI_TARGET) -std=c11 -O2 -Wall -Wextra -Werror \
    -ffreestanding -fshort-wchar -mno-red-zone \
    -fno-stack-protector -fno-builtin -nostdlib -Iinclude

.PHONY: qboot-uefi test-qboot-uefi

kernel64-uefi.elf: $(OBJS_64) linker64.ld
>$(LD) $(LDFLAGS_64) -e _start64_uefi -o $@ $(OBJS_64)

qboot-uefi: kernel64-uefi.elf
>mkdir -p $(UEFI_BUILD_DIR) $(UEFI_ESP_DIR)/EFI/BOOT $(UEFI_ESP_DIR)/EFI/QMJYOS
>$(UEFI_CC) $(UEFI_CFLAGS) -c boot/uefi/efi_main.c -o $(UEFI_BUILD_DIR)/efi_main.obj
>$(UEFI_CC) $(UEFI_CFLAGS) -c kernel/core/boot_info.c -o $(UEFI_BUILD_DIR)/boot_info.obj
>$(UEFI_LD) /machine:x64 /subsystem:efi_application /entry:efi_main /nodefaultlib \
>    /out:$(UEFI_ESP_DIR)/EFI/BOOT/BOOTX64.EFI \
>    $(UEFI_BUILD_DIR)/efi_main.obj $(UEFI_BUILD_DIR)/boot_info.obj
>cp kernel64-uefi.elf $(UEFI_ESP_DIR)/EFI/QMJYOS/KERNEL64-UEFI.ELF
>file $(UEFI_ESP_DIR)/EFI/BOOT/BOOTX64.EFI
>llvm-readobj --file-headers $(UEFI_ESP_DIR)/EFI/BOOT/BOOTX64.EFI
>llvm-readobj --file-headers --program-headers kernel64-uefi.elf

test-qboot-uefi: qboot-uefi
>bash tools/qboot/test_uefi_boot.sh
'''.replace("\n test-qboot-uefi:", "\ntest-qboot-uefi:")
# Update memory log text for both boot sources.
s = s.replace('''	python3 tests/kernel/boot_info_multiboot_test.c''', '''	python3 tests/kernel/boot_info_multiboot_test.c''')
# Add the UEFI test executable to the foundation target list and compile rules.
if "$(HOST_KERNEL_FOUNDATION_DIR)/boot-info-uefi" not in s:
    s = s.replace(
'''$(HOST_KERNEL_FOUNDATION_DIR)/boot-info-multiboot
''',
'''$(HOST_KERNEL_FOUNDATION_DIR)/boot-info-multiboot \\
$(HOST_KERNEL_FOUNDATION_DIR)/boot-info-uefi
''', 1)
    needle = '''$(HOST_KERNEL_FOUNDATION_DIR)/boot-info-multiboot: tests/kernel/boot_info_multiboot_test.c kernel/core/boot_info.c | $(HOST_KERNEL_FOUNDATION_DIR)
>$(HOST_CC) $(HOST_TEST_CFLAGS) -Iinclude -o $@ $^
'''
    add = needle + '''
$(HOST_KERNEL_FOUNDATION_DIR)/boot-info-uefi: tests/kernel/boot_info_uefi_test.c kernel/core/boot_info.c | $(HOST_KERNEL_FOUNDATION_DIR)
>$(HOST_CC) $(HOST_TEST_CFLAGS) -Iinclude -o $@ $^
'''
    if needle not in s:
        raise SystemExit("Makefile boot-info-multiboot rule not found")
    s = s.replace(needle, add, 1)
# Add UEFI output cleanup only to the existing clean recipe.
if "rm -f kernel64-uefi.elf" not in s:
    s = s.replace("rm -f kernel.elf kernel64.elf kernel64.bin kernel64.o",
                  "rm -f kernel.elf kernel64.elf kernel64-uefi.elf kernel64.bin kernel64.o", 1)
write(rel, s)

# ---------------------------------------------------------------------------
# QBuild source inventory: EFI source is tracked separately from kernel objects.
# ---------------------------------------------------------------------------
rel = "tools/qbuild/module_plan.py"
s = read(rel)
if "SOURCES_EFI =" not in s:
    s = s.replace(
'''# User-space foundation services.
''',
'''SOURCES_EFI = {
    "QBOOT_UEFI": [
        ("boot/uefi/efi_main.c", "build/qboot-uefi/efi_main.obj"),
    ],
}

# User-space foundation services.
''', 1)
    s = s.replace(
'''    for module, entries in SOURCES_USER.items():
        result[module] = result.get(module, []) + [
            (source, obj, "user") for source, obj in entries
        ]
    return result
''',
'''    for module, entries in SOURCES_USER.items():
        result[module] = result.get(module, []) + [
            (source, obj, "user") for source, obj in entries
        ]
    for module, entries in SOURCES_EFI.items():
        result[module] = result.get(module, []) + [
            (source, obj, "efi") for source, obj in entries
        ]
    return result
''', 1)
    s = s.replace(
'''    userspace_sources = []
''',
'''    userspace_sources = []
    efi_sources = []
''', 1)
    s = s.replace(
'''            else:
                userspace_sources.append(source)

    return order, objects32, objects64, userspace_sources
''',
'''            elif bits == "efi":
                efi_sources.append(source)
            else:
                userspace_sources.append(source)

    return order, objects32, objects64, userspace_sources, efi_sources
''', 1)
    s = s.replace(
'''def write_makefile(order, objects32, objects64, userspace_sources):
''',
'''def write_makefile(order, objects32, objects64, userspace_sources, efi_sources):
''', 1)
    s = s.replace(
'''        "USERSPACE_SOURCES := " + " ".join(userspace_sources),
''',
'''        "USERSPACE_SOURCES := " + " ".join(userspace_sources),
        "UEFI_SOURCES := " + " ".join(efi_sources),
''', 1)
    s = s.replace(
'''        order, objects32, objects64, userspace_sources = make_plan(
''',
'''        order, objects32, objects64, userspace_sources, efi_sources = make_plan(
''', 1)
    s = s.replace(
'''                order, objects32, objects64, userspace_sources
''',
'''                order, objects32, objects64, userspace_sources, efi_sources
''', 1)
    s = s.replace(
'''            print(f"用户空间源码：{len(userspace_sources)}")
''',
'''            print(f"用户空间源码：{len(userspace_sources)}")
            print(f"UEFI 源码：{len(efi_sources)}")
''', 1)
write(rel, s)

# Extend QBuild's target dispatcher: x86_64 + UEFI uses the dedicated PE/ELF
# target, while AArch64 remains explicitly unsupported.
rel = "tools/qbuild/qbuild_core.py"
s2 = read(rel)
old = '''    if arch != "x86_64" or boot != "bios":
        raise RuntimeError(f"目标尚未实现：{arch} + {boot}；当前仅支持 x86_64 + bios")
'''
new = '''    if arch != "x86_64":
        raise RuntimeError(f"目标尚未实现：{arch} + {boot}；当前仅支持 x86_64 + bios/uefi")
'''
if s2.count(old) != 1:
    raise SystemExit(f"qbuild_core target validation marker count: {s2.count(old)}")
s2 = s2.replace(old, new, 1)
old = '''    if preflight != 0:
        return preflight
    if not selected:
'''
new = '''    if preflight != 0:
        return preflight
    if args.boot == "uefi":
        if selected and any(module not in {"QBOOT", "QBOOT_UEFI"} for module in selected):
            raise ValueError("UEFI 启动目标需要完整内核映像；请勿使用局部内核模块选择")
        plan_result = run_tracked(
            root, "plan", [sys.executable, str(root / "tools/qbuild/module_plan.py"), "--make"], context
        )
        if plan_result != 0:
            return plan_result
        return run_tracked(
            root, "build", ["make", f"-j{args.jobs}", "qboot-uefi"], context
        )
    if not selected:
'''
if s2.count(old) != 1:
    raise SystemExit(f"qbuild_core build branch marker count: {s2.count(old)}")
s2 = s2.replace(old, new, 1)
old = '''def command_test(root: Path, args: argparse.Namespace) -> int:
    validate_target(args.arch, args.boot, args.jobs)
    context = {"arch": args.arch, "boot": args.boot, "jobs": args.jobs, "targets": list(DEFAULT_TEST_TARGETS)}
'''
new = '''def command_test(root: Path, args: argparse.Namespace) -> int:
    validate_target(args.arch, args.boot, args.jobs)
    targets = list(DEFAULT_TEST_TARGETS)
    if args.boot == "uefi":
        targets.append("test-qboot-uefi")
    context = {"arch": args.arch, "boot": args.boot, "jobs": args.jobs, "targets": targets}
'''
if s2.count(old) != 1:
    raise SystemExit(f"qbuild_core test setup marker count: {s2.count(old)}")
s2 = s2.replace(old, new, 1)
old = '''    command = ["make", f"-j{args.jobs}", *DEFAULT_TEST_TARGETS]
    return run_tracked(root, "test", command, context)
'''
new = '''    command = ["make", f"-j{args.jobs}", *targets]
    return run_tracked(root, "test", command, context)
'''
if s2.count(old) != 1:
    raise SystemExit(f"qbuild_core test command marker count: {s2.count(old)}")
s2 = s2.replace(old, new, 1)
write(rel, s2)

rel = "tests/qbuild/test_qbuild_core.py"
s2 = read(rel)
needle = '''    def test_cli_compatibility(self):
'''
addition = '''    def test_target_matrix_accepts_x86_64_uefi(self):
        q.validate_target("x86_64", "uefi", 2)
        with self.assertRaisesRegex(RuntimeError, "目标尚未实现"):
            q.validate_target("aarch64", "uefi", 2)

'''
if s2.count(needle) != 1:
    raise SystemExit("QBuild target test insertion point changed")
s2 = s2.replace(needle, addition + needle, 1)
write(rel, s2)

# Make CI exercise the newly supported x86_64 UEFI target under OVMF.
rel = ".github/workflows/c-cpp.yml"
s2 = read(rel)
if s2.count("check_rejected_target x86_64 uefi") != 1:
    raise SystemExit("CI still-unsupported UEFI check marker changed")
s2 = s2.replace("check_rejected_target x86_64 uefi", "check_rejected_target aarch64 uefi", 1)
needle = '''      - name: Verify unsupported targets are rejected
'''
addition = '''      - name: Validate x86_64 UEFI target configuration
        run: |
          sh tools/qbuild/qbuild config \\
            --arch x86_64 \\
            --boot uefi \\
            --jobs 4

'''
if s2.count(needle) != 1:
    raise SystemExit("CI unsupported-target step insertion point changed")
s2 = s2.replace(needle, addition + needle, 1)
needle = '''      - name: Validate QBuild dependency plan
        run: |
          sh tools/qbuild/qbuild plan \\
            --arch x86_64 \\
            --boot bios \\
            --jobs 4
'''
addition = needle + '''
      - name: Validate x86_64 UEFI dependency plan
        run: |
          sh tools/qbuild/qbuild plan \\
            --arch x86_64 \\
            --boot uefi \\
            --jobs 4
'''
if s2.count(needle) != 1:
    raise SystemExit("CI BIOS dependency plan marker changed")
s2 = s2.replace(needle, addition, 1)
if "uefi-build-and-test:" not in s2:
    s2 += r'''

  uefi-build-and-test:
    name: x86_64 UEFI build and OVMF boot test
    runs-on: ubuntu-latest
    timeout-minutes: 25
    needs:
      - code-and-config

    steps:
      - name: Checkout repository
        uses: actions/checkout@v4

      - name: Install UEFI build and runtime dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y \
            clang \
            lld \
            llvm \
            make \
            python3 \
            qemu-system-x86 \
            ovmf

      - name: Build QBoot UEFI and x86_64 kernel with QBuild
        run: |
          sh tools/qbuild/qbuild build \
            --arch x86_64 \
            --boot uefi \
            --jobs 4

      - name: Verify EFI PE/COFF and kernel ELF64 formats
        run: |
          file build/qboot-uefi/esp/EFI/BOOT/BOOTX64.EFI
          llvm-readobj --file-headers build/qboot-uefi/esp/EFI/BOOT/BOOTX64.EFI
          llvm-readobj --file-headers --program-headers kernel64-uefi.elf

      - name: Run BIOS regression and UEFI OVMF runtime tests
        env:
          OVMF_CODE: /usr/share/OVMF/OVMF_CODE.fd
          OVMF_VARS: /usr/share/OVMF/OVMF_VARS.fd
        run: |
          sh tools/qbuild/qbuild test \
            --arch x86_64 \
            --boot uefi \
            --jobs 4

      - name: Upload UEFI boot artifacts and runtime evidence
        if: success()
        uses: actions/upload-artifact@v4
        with:
          name: qmjyOS-x86_64-uefi-boot
          if-no-files-found: error
          retention-days: 14
          path: |
            kernel64-uefi.elf
            build/qboot-uefi/esp/EFI/BOOT/BOOTX64.EFI
            build/qboot-uefi/esp/EFI/QMJYOS/KERNEL64-UEFI.ELF
            build/test-evidence/qboot-uefi/*.log
'''
write(rel, s2)

# Keep QBOOT_UEFI planned until the user's local OVMF runtime test passes.
rel = "config/modules.status"
s = read(rel).replace("QBOOT_UEFI=planned", "QBOOT_UEFI=planned", 1)
write(rel, s)

# Update the docs without claiming complete support before runtime verification.
rel = "boot/README.md"
s = read(rel)
s = s.replace(
'''- x86_64 UEFI application
''',
'''- Complete x86_64 UEFI boot path (initial EFI loader and kernel handoff are under development; OVMF runtime verification is still required)
''', 1)
if "UEFI first implementation" not in s:
    s += r'''

## UEFI first implementation scope

The `qboot-uefi` target builds `EFI/BOOT/BOOTX64.EFI` and an x86_64 kernel ELF
whose entry is `_start64_uefi`. The linker page-aligns RX/R/RW ELF segments; the
loader stages all PT_LOAD segments as EFI_LOADER_DATA to avoid firmware
write-protection during copying, then the kernel replaces the firmware page
 tables before executing the image. It gathers GOP and
ACPI information, normalizes the UEFI memory
map into `qm_boot_info_t`, and calls ExitBootServices with MapKey retry.

The firmware entry and ELF format can be built independently. This does not
constitute a supported release until `make test-qboot-uefi` passes on OVMF and
BIOS regression tests continue to pass. Secure Boot, runtime services, a full
boot menu, and final hybrid ISO authoring remain separate work.
'''
write(rel, s)

rel = "boot/protocols/README.md"
s = read(rel)
if "UEFI implementation handoff details" not in s:
    s += r'''

## UEFI implementation handoff details

The x86_64 UEFI loader uses the Microsoft x64 ABI while calling firmware
services. It disables the firmware watchdog before the final memory-map and
ExitBootServices sequence. After a successful ExitBootServices(), it transfers to the kernel
using the System V x86_64 ABI with `RDI = qm_boot_info_t *` and interrupts
disabled. It does not call Boot Services again and does not call
SetVirtualAddressMap; the firmware's pre-virtual-map identity mappings remain
in place until the kernel installs its own page tables.

Protocol v1.1 adds `page_table_root_physical`. UEFI prepares a supervisor-only
1-GiB identity map using 2-MiB pages before ExitBootServices; the kernel entry
switches to that root only after moving to its own low-memory stack. This keeps
the current kernel's first-GiB assumptions aligned with the BIOS bootstrap.

The UEFI memory map is converted from its firmware-specific descriptor stride
to `qm_boot_memory_region_t`. Conventional memory is usable; loader and boot
service allocations remain bootloader-reclaimable; ACPI reclaim/NVS and
unusable memory retain distinct types; unrecognized types are reserved. The
loaded kernel's physical range is marked `QM_MEMORY_KERNEL`.

The legacy BIOS Multiboot v1 bridge remains supported and continues to call
`main(magic, info_addr)`. UEFI uses the alternate ELF entry `_start64_uefi`
and calls `main_uefi(boot_info)`. These paths converge on the same ordered
kernel initialization and scheduler.
'''
write(rel, s)

# Validate expected patched state before reporting success.
required_after = {
    "include/qmjy/boot.h": "qm_boot_info_from_uefi(",
    "include/qmjy/boot_offsets.h": "QM_BOOT_INFO_PAGE_TABLE_ROOT_OFFSET 152",
    "kernel/core/boot_info.c": "qm_boot_map_uefi_type",
    "arch/x86_64/boot/entry64.S": "_start64_uefi:",
    "kernel/core/main.c": "void main_uefi(qm_boot_info_t *boot_info)",
    "Makefile": "test-qboot-uefi:",
    "linker64.ld": "    . = ALIGN(4096);\n    .data :",
    "tools/qbuild/module_plan.py": "SOURCES_EFI =",
    "tools/qbuild/qbuild_core.py": 'if args.boot == "uefi":',
    "tests/qbuild/test_qbuild_core.py": "test_target_matrix_accepts_x86_64_uefi",
    ".github/workflows/c-cpp.yml": "uefi-build-and-test:",
}
for rel, marker in required_after.items():
    if marker not in read(rel):
        print(f"[UEFI PATCH FAIL] Postcondition failed: {rel}: {marker}")
        sys.exit(5)
if "Physical page allocator: initialized from qm_boot_info_t memory map." not in read("kernel/core/main.c"):
    print("[UEFI PATCH FAIL] UEFI PMM log adapter was not inserted")
    sys.exit(5)
if "g_kernel_boot_info.firmware == QM_BOOT_FIRMWARE_BIOS ? 88U : 0" not in read("kernel/core/main.c"):
    print("[UEFI PATCH FAIL] UEFI legacy Multiboot reservation parameters were not cleared")
    sys.exit(5)
if "offsetof(qm_boot_info_t, page_table_root_physical)" not in read("kernel/core/boot_info.c"):
    print("[UEFI PATCH FAIL] Protocol page-table-root offset assertion is missing")
    sys.exit(5)
if "mov QM_BOOT_INFO_PAGE_TABLE_ROOT_OFFSET(%r12), %rax" not in read("arch/x86_64/boot/entry64.S"):
    print("[UEFI PATCH FAIL] UEFI entry does not switch to the QBoot kernel page tables")
    sys.exit(5)
if "build_kernel_identity_page_tables(" not in read("boot/uefi/efi_main.c"):
    print("[UEFI PATCH FAIL] UEFI page tables are not constructed")
    sys.exit(5)
if "test-qboot-uefi" not in read("tools/qbuild/qbuild_core.py"):
    print("[UEFI PATCH FAIL] QBuild test command is not connected to UEFI runtime validation")
    sys.exit(5)
if "check_rejected_target x86_64 uefi" in read(".github/workflows/c-cpp.yml"):
    print("[UEFI PATCH FAIL] CI still rejects x86_64 UEFI")
    sys.exit(5)

try:
    for rel, content in STAGED.items():
        path = ROOT / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8", newline="\n")
        if rel in EXECUTABLES:
            path.chmod(0o755)
except OSError as error:
    print(f"[UEFI PATCH FAIL] Worktree write failed: {error}")
    for rel in MODIFIED:
        backup = BACKUP_DIR / rel
        if backup.exists():
            shutil.copy2(backup, ROOT / rel)
    for rel in NEW_FILES:
        path = ROOT / rel
        if path.exists():
            path.unlink()
    sys.exit(6)

print("[UEFI PATCH PASS] Initial dual-path UEFI implementation applied.")
print("[UEFI PATCH] Backups: build/uefi-phase1-backup/")
print("[UEFI PATCH] Next validation commands:")
print("  python3 -m py_compile tools/qbuild/module_plan.py tools/qbuild/qbuild_core.py")
print("  python3 tools/qbuild/module_plan.py --check --make")
print("  sh tools/qbuild/qbuild self-test")
print("  make test-kernel-foundation")
print("  sh tools/qbuild/qbuild build --arch x86_64 --boot uefi --jobs 2")
print("  sh tools/qbuild/qbuild test --arch x86_64 --boot uefi --jobs 2")
print(f"[UEFI PATCH PASS] Original tracked files backed up under: {BACKUP_DIR.relative_to(ROOT)}")
print("[UEFI PATCH INFO] Existing unrelated untracked files were not touched.")
print("Next: run the validation commands printed by the assistant; do not publish yet.")
