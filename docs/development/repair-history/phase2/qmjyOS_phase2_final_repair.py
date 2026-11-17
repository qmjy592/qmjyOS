#!/usr/bin/env python3
"""Apply the Phase 2 final-repair tranche to qmjyOS (local working tree only).

The script is deliberately transactional: all source anchors are checked before
writes, original files are backed up under build/backups/, and partial writes
are rolled back on failure. It does not commit, push, or contact GitHub.
"""
from __future__ import annotations

from pathlib import Path
import os
import re
import shutil
import sys

ROOT = Path.cwd()
BACKUP = ROOT / "build" / "backups" / "phase2-final-20261116"
MARKER = ROOT / "build" / "phase2-final-20261116.applied"


def fail(message: str) -> None:
    raise RuntimeError(message)


def read(path: str) -> str:
    p = ROOT / path
    if not p.is_file():
        fail(f"Required file is missing: {path}")
    return p.read_text(encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        fail(f"Anchor check failed for {label}: expected exactly 1, found {count}")
    return text.replace(old, new, 1)


def write_new(path: str, content: str, updates: dict[str, str], new_files: dict[str, str]) -> None:
    if (ROOT / path).exists():
        fail(f"New file already exists; refusing to overwrite: {path}")
    new_files[path] = content


if not (ROOT / ".git").exists() or not (ROOT / "Makefile").is_file():
    fail("Run this script from the root of the qmjyOS Git working tree.")
if MARKER.exists():
    fail("This Phase 2 repair tranche is already marked as applied. Inspect git diff before rerunning.")
if BACKUP.exists():
    fail(f"Backup directory already exists: {BACKUP}. Inspect it before retrying.")

updates: dict[str, str] = {}
new_files: dict[str, str] = {}

# ---------------------------------------------------------------------------
# 1. Standardize QBoot's selectors with the Multiboot/kernel GDT contract.
#    Selector 0x08 is always 64-bit kernel code; 0x10 is data; 0x18 is 32-bit
#    protected-mode code used only during the BIOS transition.
# ---------------------------------------------------------------------------
p = "boot/bios/qboot_stage2.S"
s = read(p)
s = replace_once(s, "    ljmpl $0x08, $protected_entry\n", "    ljmpl $0x18, $protected_entry\n", "QBoot protected-mode selector")
s = replace_once(s, "    ljmp $0x18, $long_mode_entry\n", "    ljmp $0x08, $long_mode_entry\n", "QBoot long-mode selector")
s = replace_once(
    s,
    "gdt:\n    .quad 0\n    .quad 0x00CF9A000000FFFF\n    .quad 0x00CF92000000FFFF\n    .quad 0x00AF9A000000FFFF\n",
    "gdt:\n    .quad 0\n    .quad 0x00AF9A000000FFFF  # 0x08: 64-bit kernel code\n    .quad 0x00CF92000000FFFF  # 0x10: data\n    .quad 0x00CF9A000000FFFF  # 0x18: 32-bit protected-mode code\n",
    "QBoot GDT descriptor order",
)
updates[p] = s

# Read CS instead of assuming a bootloader's selector value in early entry.
p = "arch/x86_64/boot/entry64.S"
s = read(p)
s = replace_once(
    s,
    "    # The active Multiboot long-mode code selector is 0x08.\n    mov $0x08, %edi\n    call idt_init_with_selector\n",
    "    # Use the selector that actually brought us into 64-bit mode.\n    # Both supported BIOS handoffs standardize this as 0x08, but reading CS\n    # keeps the early IDT correct if another compatible loader uses a peer GDT.\n    xor %edi, %edi\n    movw %cs, %di\n    call idt_init_with_selector\n",
    "early IDT selector from CS",
)
updates[p] = s

# ---------------------------------------------------------------------------
# 2. Make the runtime GDT switch precede the replacement IDT installation.
#    Once QBoot and Multiboot agree on 0x08, the early IDT remains valid across
#    this switch; the new IDT is installed only after the kernel GDT is active.
# ---------------------------------------------------------------------------
p = "kernel/core/main.c"
s = read(p)
s = replace_once(
    s,
    "static uint32_t g_boot_multiboot_magic;\nstatic uint32_t g_boot_multiboot_info_addr;\n",
    "static uint32_t g_boot_multiboot_magic;\nstatic uint32_t g_boot_multiboot_info_addr;\nstatic uint32_t g_boot_multiboot_mmap_addr;\nstatic uint32_t g_boot_multiboot_mmap_length;\n",
    "stored Multiboot mmap bounds",
)
s = replace_once(
    s,
    "    pic_remap();\n    idt_init();\n    timer_init(100);\n    init_gdt_and_tss();\n",
    "    init_gdt_and_tss();\n    pic_remap();\n    idt_init();\n    timer_init(100);\n",
    "architecture initialization order",
)
s = replace_once(
    s,
    "    status = mm_init_from_multiboot(\n        g_boot_multiboot_magic, g_boot_multiboot_info_addr,\n        (uint64_t)(uintptr_t)&__kernel64_start,\n        (uint64_t)(uintptr_t)&__kernel64_end);\n",
    "    status = mm_init_from_boot_info(\n        &g_kernel_boot_info,\n        g_boot_multiboot_info_addr, 88U,\n        g_boot_multiboot_mmap_addr, g_boot_multiboot_mmap_length);\n",
    "PMM consumes normalized Boot Info",
)
s = replace_once(
    s,
    "    uint32_t mmap_addr;\n",
    "    uint32_t mmap_addr;\n    uint32_t mmap_length;\n",
    "local Multiboot mmap length declaration",
)
s = replace_once(
    s,
    "    mmap_addr = ((const volatile uint32_t *)(uintptr_t)multiboot_info_addr)[12];\n",
    "    mmap_length = ((const volatile uint32_t *)(uintptr_t)multiboot_info_addr)[11];\n    mmap_addr = ((const volatile uint32_t *)(uintptr_t)multiboot_info_addr)[12];\n    g_boot_multiboot_mmap_addr = mmap_addr;\n    g_boot_multiboot_mmap_length = mmap_length;\n",
    "capture both Multiboot mmap address and length",
)
s = replace_once(
    s,
    "    if (kernel_end <= kernel_base || mmap_addr == 0) {\n",
    "    if (kernel_end <= kernel_base || mmap_addr == 0 || mmap_length == 0) {\n",
    "reject missing Multiboot mmap length",
)
updates[p] = s

# ---------------------------------------------------------------------------
# 3. Strengthen generic Boot Info kernel virtual-range validation.
# ---------------------------------------------------------------------------
p = "kernel/core/boot_info.c"
s = read(p)
s = replace_once(
    s,
    "    uint64_t map_bytes;\n    uint64_t module_bytes;\n",
    "    uint64_t map_bytes;\n    uint64_t module_bytes;\n    uint64_t kernel_virtual_end;\n",
    "Boot Info virtual-end local",
)
s = replace_once(
    s,
    "    if (info->kernel_physical_size == 0 ||\n        range_overflows(info->kernel_physical_base,\n                        info->kernel_physical_size) ||\n        info->kernel_entry_virtual == 0)\n        return QM_ERR_CORRUPT;\n",
    "    if (info->kernel_physical_size == 0 ||\n        info->kernel_entry_virtual == 0)\n        return QM_ERR_CORRUPT;\n\n    if (range_overflows(info->kernel_physical_base,\n                        info->kernel_physical_size) ||\n        range_overflows(info->kernel_virtual_base,\n                        info->kernel_physical_size))\n        return QM_ERR_OVERFLOW;\n\n    kernel_virtual_end = info->kernel_virtual_base +\n                         info->kernel_physical_size;\n    if (info->kernel_entry_virtual < info->kernel_virtual_base ||\n        info->kernel_entry_virtual >= kernel_virtual_end)\n        return QM_ERR_CORRUPT;\n",
    "Boot Info physical and virtual kernel bounds",
)
updates[p] = s

# ---------------------------------------------------------------------------
# 4. Add a normalized Boot Info PMM initialization path. The Multiboot parser
#    remains available for compatibility tests, but main() no longer calls it.
# ---------------------------------------------------------------------------
p = "mm/physical/memory.h"
s = read(p)
s = replace_once(
    s,
    "#include <stdint.h>\n",
    "#include <stdint.h>\n#include \"qmjy/boot.h\"\n",
    "PMM Boot Info type include",
)
s = replace_once(
    s,
    "int mm_init_from_multiboot(uint32_t magic, uint32_t info_addr, uint64_t kernel_start, uint64_t kernel_end);\n",
    "int mm_init_from_multiboot(uint32_t magic, uint32_t info_addr, uint64_t kernel_start, uint64_t kernel_end);\nint mm_init_from_boot_info(const qm_boot_info_t *info,\n                           uint64_t bootloader_info_base,\n                           uint64_t bootloader_info_size,\n                           uint64_t bootloader_map_base,\n                           uint64_t bootloader_map_size);\n",
    "PMM normalized Boot Info API declaration",
)
updates[p] = s

p = "mm/physical/memory.c"
s = read(p)
s = replace_once(
    s,
    "#include \"memory.h\"\n",
    "#include \"memory.h\"\n#include <stddef.h>\n",
    "PMM offsetof include",
)
start = s.find("int mm_parse_multiboot_mmap(uint32_t addr, uint32_t length) {")
end = s.find("/* Multiboot v1 info layout: flags @0, mmap_length @44, mmap_addr @48. */", start)
if start < 0 or end < 0 or end <= start:
    fail("Could not locate the complete Multiboot mmap parser for replacement")
new_parser = r'''static int mm_parse_boot_regions(const void *input,
                                 size_t count,
                                 size_t entry_size)
{
    const uint8_t *bytes = (const uint8_t *)input;
    int error = MM_ERR_FORMAT;

    allocator_disable();
    firmware_reserved_count = 0;

    if (input == NULL || count == 0 || count > MM_MAX_RANGES ||
        entry_size < sizeof(qm_boot_memory_region_t) ||
        count > (size_t)-1 / entry_size)
        return MM_ERR_ARGUMENT;

    for (size_t i = 0; i < count; ++i) {
        qm_boot_memory_region_t region;
        uint64_t base;
        uint64_t length;
        uint64_t end;

        __builtin_memcpy(&region, bytes + i * entry_size, sizeof(region));
        base = region.base;
        length = region.length;

        if (length == 0 || base > UINT64_MAX - length)
            goto fail;
        end = base + length;

        if (region.type != QM_MEMORY_USABLE) {
            uint64_t lo;
            uint64_t hi;

            if (base >= MM_LIMIT)
                continue;
            if (firmware_reserved_count >= MM_MAX_RANGES) {
                error = MM_ERR_CAPACITY;
                goto fail;
            }

            lo = align_down(base);
            if (end >= MM_LIMIT)
                hi = MM_LIMIT;
            else if (!align_up_checked(end, &hi))
                hi = MM_LIMIT;

            if (hi > lo) {
                firmware_reserved_base[firmware_reserved_count] = lo;
                firmware_reserved_len[firmware_reserved_count] = hi - lo;
                ++firmware_reserved_count;
            }
            continue;
        }

        if (base < MM_LIMIT) {
            uint64_t lo;
            uint64_t hi;

            if (!align_up_checked(base, &lo))
                goto fail;
            if (end > MM_LIMIT)
                end = MM_LIMIT;
            hi = align_down(end);

            if (hi > lo) {
                if (ranges >= MM_MAX_RANGES) {
                    error = MM_ERR_CAPACITY;
                    goto fail;
                }
                usable[ranges] = lo;
                usable_len[ranges] = hi - lo;
                ++ranges;
            }
        }
    }

    if (ranges == 0) {
        error = MM_ERR_NO_MEMORY;
        goto fail;
    }

    /* Release every usable page once, even if firmware ranges overlap. */
    for (size_t r = 0; r < ranges; ++r) {
        uint64_t end = usable[r] + usable_len[r];
        for (uint64_t pa = usable[r]; pa < end; pa += MM_PAGE_SIZE) {
            size_t page = (size_t)(pa / MM_PAGE_SIZE);
            if (page < MM_MAX_PAGES && bit_get(page)) {
                bit_clear(page);
                ++free_count;
            }
        }
    }

    /* Non-usable firmware ranges override every overlapping usable range. */
    for (size_t f = 0; f < firmware_reserved_count; ++f) {
        uint64_t reserve_start = firmware_reserved_base[f];
        uint64_t reserve_end = reserve_start + firmware_reserved_len[f];

        for (size_t r = 0; r < ranges; ++r) {
            uint64_t range_end = usable[r] + usable_len[r];
            uint64_t lo = reserve_start > usable[r] ? reserve_start : usable[r];
            uint64_t hi = reserve_end < range_end ? reserve_end : range_end;

            for (uint64_t pa = lo; pa < hi; pa += MM_PAGE_SIZE) {
                size_t page = (size_t)(pa / MM_PAGE_SIZE);
                if (!bit_get(page)) {
                    bit_set(page);
                    if (free_count != 0)
                        --free_count;
                }
            }
        }
    }

    /* Apply explicit boot and caller reservations without double subtraction. */
    for (size_t r = 0; r < reserved_count; ++r) {
        uint64_t end = reserved_base[r] + reserved_len[r];
        uint64_t lo = align_down(reserved_base[r]);
        uint64_t hi;

        if (end >= MM_LIMIT)
            hi = MM_LIMIT;
        else if (!align_up_checked(end, &hi))
            hi = MM_LIMIT;
        if (lo > MM_LIMIT)
            lo = MM_LIMIT;

        for (uint64_t pa = lo; pa < hi; pa += MM_PAGE_SIZE) {
            size_t page = (size_t)(pa / MM_PAGE_SIZE);
            if (page < MM_MAX_PAGES && !bit_get(page)) {
                bit_set(page);
                if (free_count != 0)
                    --free_count;
            }
        }
    }

    if (free_count == 0) {
        error = MM_ERR_NO_MEMORY;
        goto fail;
    }

    total_pages = free_count;
    initialized = 1;
    return MM_OK;

fail:
    allocator_disable();
    firmware_reserved_count = 0;
    return error;
}

int mm_parse_multiboot_mmap(uint32_t addr, uint32_t length)
{
    qm_boot_memory_region_t parsed[MM_MAX_RANGES];
    size_t count = 0;
    uint32_t off = 0;

    allocator_disable();
    firmware_reserved_count = 0;

    /* The legacy parser dereferences this map through the first-GiB identity map. */
    if (addr == 0 || length == 0 || (uint64_t)addr + length > MM_LIMIT)
        return MM_ERR_ARGUMENT;

    while (off < length) {
        const uint8_t *p;
        uint32_t size;
        uint32_t type;
        uint64_t base;
        uint64_t region_length;

        if (length - off < 4U)
            goto malformed;
        p = (const uint8_t *)(uintptr_t)((uint64_t)addr + off);
        __builtin_memcpy(&size, p, sizeof(size));
        if (size < 20U || size > length - off - 4U)
            goto malformed;

        __builtin_memcpy(&base, p + 4U, sizeof(base));
        __builtin_memcpy(&region_length, p + 12U, sizeof(region_length));
        __builtin_memcpy(&type, p + 20U, sizeof(type));

        if (region_length != 0) {
            if (base > UINT64_MAX - region_length)
                goto malformed;
            if (count >= MM_MAX_RANGES) {
                allocator_disable();
                return MM_ERR_CAPACITY;
            }
            parsed[count].base = base;
            parsed[count].length = region_length;
            switch (type) {
            case 1: parsed[count].type = QM_MEMORY_USABLE; break;
            case 3: parsed[count].type = QM_MEMORY_ACPI_RECLAIM; break;
            case 4: parsed[count].type = QM_MEMORY_ACPI_NVS; break;
            case 5: parsed[count].type = QM_MEMORY_BAD; break;
            default: parsed[count].type = QM_MEMORY_RESERVED; break;
            }
            parsed[count].attributes = 1U;
            ++count;
        }
        off += size + 4U;
    }

    if (count == 0) {
        allocator_disable();
        return MM_ERR_NO_MEMORY;
    }
    return mm_parse_boot_regions(parsed, count, sizeof(parsed[0]));

malformed:
    allocator_disable();
    firmware_reserved_count = 0;
    return MM_ERR_FORMAT;
}

'''
s = s[:start] + new_parser + s[end:]
# Add the normalized Boot Info entry point immediately after the legacy initializer.
anchor = """int mm_alloc_page(uint64_t *out) {
"""
if s.count(anchor) != 1:
    fail("Could not locate mm_alloc_page insertion anchor")
boot_api = r'''int mm_init_from_boot_info(const qm_boot_info_t *info,
                           uint64_t bootloader_info_base,
                           uint64_t bootloader_info_size,
                           uint64_t bootloader_map_base,
                           uint64_t bootloader_map_size)
{
    uintptr_t info_address;
    uint64_t map_bytes;
    uint64_t module_bytes = 0;
    const qm_boot_memory_region_t *regions;
    const qm_boot_module_t *modules = NULL;
    int rc;

    allocator_disable();
    remove_boot_reservations();
    firmware_reserved_count = 0;

    if (info == NULL)
        return MM_ERR_ARGUMENT;
    if (qm_boot_validate_info(info) != QM_OK ||
        info->architecture != QM_BOOT_ARCH_X86_64)
        return MM_ERR_FORMAT;

    info_address = (uintptr_t)info;
    if ((uint64_t)info_address >= MM_LIMIT ||
        (uint64_t)info->struct_size > MM_LIMIT - (uint64_t)info_address)
        return MM_ERR_ARGUMENT;

    if (info->memory_map_count > MM_MAX_RANGES ||
        info->memory_map_entry_size < sizeof(qm_boot_memory_region_t))
        return MM_ERR_FORMAT;
    map_bytes = (uint64_t)info->memory_map_count *
                (uint64_t)info->memory_map_entry_size;
    if (info->memory_map_physical >= MM_LIMIT ||
        map_bytes > MM_LIMIT - info->memory_map_physical ||
        (info->memory_map_physical & (sizeof(uint64_t) - 1U)) != 0)
        return MM_ERR_FORMAT;

    regions = (const qm_boot_memory_region_t *)(uintptr_t)
              info->memory_map_physical;

    if ((bootloader_info_size != 0 &&
         bootloader_info_base > UINT64_MAX - bootloader_info_size) ||
        (bootloader_map_size != 0 &&
         bootloader_map_base > UINT64_MAX - bootloader_map_size))
        return MM_ERR_ARGUMENT;

    if (info->module_count != 0) {
        module_bytes = (uint64_t)info->module_count * sizeof(qm_boot_module_t);
        if (info->modules_physical >= MM_LIMIT ||
            module_bytes > MM_LIMIT - info->modules_physical ||
            (info->modules_physical & (sizeof(uint64_t) - 1U)) != 0)
            return MM_ERR_FORMAT;
        modules = (const qm_boot_module_t *)(uintptr_t)
                  info->modules_physical;
    }

#define MM_RESERVE_BOOT(base_, size_) do { \
        rc = mm_reserve_internal((uint64_t)(base_), (uint64_t)(size_), 1); \
        if (rc != MM_OK) goto reserve_failed; \
    } while (0)

    /* Keep low firmware memory and the historical 1-2 MiB bootstrap window. */
    MM_RESERVE_BOOT(0, 0x100000ULL);
    MM_RESERVE_BOOT(0x100000ULL, 0x100000ULL);
    MM_RESERVE_BOOT(info->kernel_physical_base,
                    info->kernel_physical_size);
    MM_RESERVE_BOOT(info_address, info->struct_size);
    MM_RESERVE_BOOT(info->memory_map_physical, map_bytes);

    /* The old loader-owned records are no longer parsed by PMM, but remain
     * reserved so the allocator cannot reuse memory still owned by firmware. */
    if (bootloader_info_size != 0)
        MM_RESERVE_BOOT(bootloader_info_base, bootloader_info_size);
    if (bootloader_map_size != 0)
        MM_RESERVE_BOOT(bootloader_map_base, bootloader_map_size);

    if (info->framebuffer.size != 0)
        MM_RESERVE_BOOT(info->framebuffer.physical_address,
                        info->framebuffer.size);

    if (info->module_count != 0) {
        MM_RESERVE_BOOT(info->modules_physical, module_bytes);
        for (qm_u32_t i = 0; i < info->module_count; ++i) {
            if (modules[i].size == 0 ||
                modules[i].physical_start > UINT64_MAX - modules[i].size) {
                rc = MM_ERR_FORMAT;
                goto reserve_failed;
            }
            MM_RESERVE_BOOT(modules[i].physical_start, modules[i].size);
        }
    }

    rc = mm_parse_boot_regions(regions, info->memory_map_count,
                               info->memory_map_entry_size);
    if (rc != MM_OK)
        goto reserve_failed;

#undef MM_RESERVE_BOOT
    return MM_OK;

reserve_failed:
    allocator_disable();
    firmware_reserved_count = 0;
    remove_boot_reservations();
#undef MM_RESERVE_BOOT
    return rc;
}

'''
s = s.replace(anchor, boot_api + anchor, 1)
updates[p] = s

# ---------------------------------------------------------------------------
# 5. Prevent VFS parent-tree and mount-root cycles.
# ---------------------------------------------------------------------------
p = "fs/vfs/vfs.c"
s = read(p)
s = replace_once(
    s,
    "static struct vfs_node *vfs_root_node;\n",
    "static struct vfs_node *vfs_root_node;\n\n#define VFS_MAX_PARENT_DEPTH 256U\n\n/* Return true for a cycle or an untrusted/corrupt parent chain. */\nstatic int parent_chain_contains(const struct vfs_node *node,\n                                 const struct vfs_node *candidate)\n{\n    size_t depth = 0;\n\n    while (node != NULL) {\n        if (node == candidate)\n            return 1;\n        if (node->parent == NULL || node->parent == node)\n            return 0;\n        node = node->parent;\n        if (++depth >= VFS_MAX_PARENT_DEPTH)\n            return 1;\n    }\n    return 0;\n}\n",
    "VFS parent-chain cycle helper",
)
s = replace_once(
    s,
    "    if (directory == NULL || child == NULL ||\n        directory->type != VFS_NODE_DIRECTORY ||\n        !valid_name(child->name) ||\n        child->parent != NULL || child->next != NULL)\n        return VFS_ERR_INVALID;\n\n    for (current = directory->children;\n",
    "    if (directory == NULL || child == NULL ||\n        directory->type != VFS_NODE_DIRECTORY ||\n        !valid_name(child->name) ||\n        child->parent != NULL || child->next != NULL)\n        return VFS_ERR_INVALID;\n\n    /* A detached subtree must never be attached below one of its descendants. */\n    if (parent_chain_contains(directory, child))\n        return VFS_ERR_INVALID;\n\n    for (current = directory->children;\n",
    "VFS add-child ancestor cycle rejection",
)
s = replace_once(
    s,
    "    if (mountpoint->mounted_root != NULL ||\n        filesystem_root == vfs_root_node)\n        return VFS_ERR_BUSY;\n\n    mountpoint->mounted_root = filesystem_root;\n",
    "    if (mountpoint->mounted_root != NULL ||\n        filesystem_root == vfs_root_node ||\n        filesystem_root == mountpoint)\n        return VFS_ERR_BUSY;\n\n    /* A filesystem root may be detached or self-rooted, but not already\n     * attached elsewhere. Mounting it below one of its own descendants\n     * would create a parent/mount cycle and is rejected. */\n    if (filesystem_root->parent != NULL &&\n        filesystem_root->parent != filesystem_root)\n        return VFS_ERR_BUSY;\n    if (parent_chain_contains(mountpoint, filesystem_root))\n        return VFS_ERR_INVALID;\n\n    mountpoint->mounted_root = filesystem_root;\n",
    "VFS mount-root cycle and ownership rejection",
)
updates[p] = s

# ---------------------------------------------------------------------------
# 6. Expand Boot Info validation tests for virtual-range overflow and bounds.
# ---------------------------------------------------------------------------
p = "tests/kernel/kernel_foundation_test.c"
s = read(p)
s = replace_once(
    s,
    "    info = valid_boot_info();\n\n    assert(qm_kernel_init_register(NULL) == QM_ERR_INVALID);\n",
    "    info = valid_boot_info();\n    info.kernel_virtual_base = UINT64_MAX - 0x7FFU;\n    info.kernel_physical_size = 0x1000U;\n    assert(qm_boot_validate_info(&info) == QM_ERR_OVERFLOW);\n    info = valid_boot_info();\n    info.kernel_entry_virtual = info.kernel_virtual_base - 1U;\n    assert(qm_boot_validate_info(&info) == QM_ERR_CORRUPT);\n    info = valid_boot_info();\n    info.kernel_entry_virtual = info.kernel_virtual_base +\n                                info.kernel_physical_size;\n    assert(qm_boot_validate_info(&info) == QM_ERR_CORRUPT);\n    info = valid_boot_info();\n\n    assert(qm_kernel_init_register(NULL) == QM_ERR_INVALID);\n",
    "Boot Info virtual-bound regression tests",
)
updates[p] = s

# Add PMM normalized Boot Info test.
new_files["tests/memory_boot_info_test.c"] = r'''#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "memory.h"
#include "qmjy/boot.h"

#define TEST_AREA_ADDR 0x10000000ULL
#define TEST_AREA_SIZE 0x4000U
#define TEST_INFO_ADDR TEST_AREA_ADDR
#define TEST_REGIONS_ADDR (TEST_AREA_ADDR + 0x1000U)
#define TEST_MB_INFO_ADDR (TEST_AREA_ADDR + 0x2000U)
#define TEST_MB_MAP_ADDR (TEST_AREA_ADDR + 0x3000U)

static void prepare_info(qm_boot_info_t *info,
                         qm_boot_memory_region_t *regions)
{
    memset(info, 0, sizeof(*info));
    memset(regions, 0, 2U * sizeof(*regions));

    info->magic = QM_BOOT_INFO_MAGIC;
    info->protocol_major = QM_BOOT_PROTOCOL_MAJOR;
    info->protocol_minor = QM_BOOT_PROTOCOL_MINOR;
    info->struct_size = (qm_u32_t)sizeof(*info);
    info->architecture = QM_BOOT_ARCH_X86_64;
    info->firmware = QM_BOOT_FIRMWARE_BIOS;
    info->memory_map_physical = TEST_REGIONS_ADDR;
    info->memory_map_count = 2;
    info->memory_map_entry_size = (qm_u32_t)sizeof(*regions);
    info->kernel_physical_base = 0x200000U;
    info->kernel_physical_size = 0x100000U;
    info->kernel_virtual_base = 0x200000U;
    info->kernel_entry_virtual = 0x200010U;

    regions[0].base = 0x100000U;
    regions[0].length = 0x17F00000U; /* Ends at 384 MiB; includes test metadata. */
    regions[0].type = QM_MEMORY_USABLE;
    regions[0].attributes = 1U;

    regions[1].base = 0x400000U;
    regions[1].length = 0x100000U;
    regions[1].type = QM_MEMORY_RESERVED;
    regions[1].attributes = 1U;
}

int main(void)
{
    void *area = mmap((void *)(uintptr_t)TEST_AREA_ADDR, TEST_AREA_SIZE,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                      -1, 0);
    qm_boot_info_t *info;
    qm_boot_memory_region_t *regions;
    uint64_t page;

    if (area == MAP_FAILED) {
        puts("SKIP: low physical-address mmap unavailable");
        return 77;
    }
    memset(area, 0, TEST_AREA_SIZE);
    info = (qm_boot_info_t *)area;
    regions = (qm_boot_memory_region_t *)((uint8_t *)area + 0x1000U);

    prepare_info(info, regions);

    /* Every usable byte in this map belongs to Boot Info, the normalized map,
     * or the loader metadata. PMM must not expose any of those pages. */
    regions[0].base = TEST_AREA_ADDR;
    regions[0].length = TEST_AREA_SIZE;
    assert(mm_init_from_boot_info(info, TEST_MB_INFO_ADDR, 88U,
                                  TEST_MB_MAP_ADDR, 48U) == MM_ERR_NO_MEMORY);
    assert(mm_alloc_page(&page) == MM_ERR_ARGUMENT);

    prepare_info(info, regions);
    assert(qm_boot_validate_info(info) == QM_OK);
    assert(mm_init_from_boot_info(info, TEST_MB_INFO_ADDR, 88U,
                                  TEST_MB_MAP_ADDR, 48U) == MM_OK);

    /* 0-2 MiB and the kernel image are reserved. The first free page is 3 MiB. */
    for (uint64_t expected = 0x300000U; expected < 0x400000U;
         expected += MM_PAGE_SIZE) {
        assert(mm_alloc_page(&page) == MM_OK);
        assert(page == expected);
    }

    /* A firmware-reserved overlap overrides the usable E820 extent. */
    assert(mm_alloc_page(&page) == MM_OK);
    assert(page == 0x500000U);
    assert(mm_free_page(0x400000U) == MM_ERR_ARGUMENT);
    assert(mm_self_test() == MM_OK);

    /* Versioned memory-map entries may be larger than the v1 structure. */
    {
        qm_boot_memory_region_t first = regions[0];
        qm_boot_memory_region_t second = regions[1];
        uint8_t *strided_map = (uint8_t *)regions;
        memset(strided_map, 0, 2U * (sizeof(first) + 8U));
        memcpy(strided_map, &first, sizeof(first));
        memcpy(strided_map + sizeof(first) + 8U, &second, sizeof(second));
        info->memory_map_entry_size = (qm_u32_t)(sizeof(first) + 8U);
        assert(mm_init_from_boot_info(info, TEST_MB_INFO_ADDR, 88U,
                                      TEST_MB_MAP_ADDR, 48U) == MM_OK);
        assert(mm_alloc_page(&page) == MM_OK && page == 0x300000U);
        assert(mm_free_page(page) == MM_OK);
    }

    /* Boot Info virtual-range overflow is rejected before PMM initialization. */
    info->kernel_virtual_base = UINT64_MAX - 0x7FFU;
    info->kernel_physical_size = 0x1000U;
    assert(mm_init_from_boot_info(info, TEST_MB_INFO_ADDR, 88U,
                                  TEST_MB_MAP_ADDR, 48U) == MM_ERR_FORMAT);
    assert(mm_alloc_page(&page) == MM_ERR_ARGUMENT);

    assert(munmap(area, TEST_AREA_SIZE) == 0);
    puts("PMM normalized Boot Info tests: PASS");
    return 0;
}
'''

# Add QBoot selector / early-IDT regression test.
new_files["tests/boot/test_qboot_gdt.py"] = r'''#!/usr/bin/env python3
"""Static selector-contract regression checks for both BIOS entry paths."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
qboot = (ROOT / "boot/bios/qboot_stage2.S").read_text(encoding="utf-8")
entry = (ROOT / "arch/x86_64/boot/entry64.S").read_text(encoding="utf-8")
main = (ROOT / "kernel/core/main.c").read_text(encoding="utf-8")
multiboot = (ROOT / "boot/bios/boot.S").read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(f"[FAIL] {message}")
    print(f"[PASS] {message}")


require("ljmpl $0x18, $protected_entry" in qboot,
        "QBoot enters 32-bit protected mode through selector 0x18")
require("ljmp $0x08, $long_mode_entry" in qboot,
        "QBoot enters 64-bit long mode through selector 0x08")
require(re.search(
    r"gdt:\s*\.quad 0\s*\.quad 0x00AF9A000000FFFF.*\n\s*\.quad 0x00CF92000000FFFF.*\n\s*\.quad 0x00CF9A000000FFFF",
    qboot), "QBoot GDT uses 64-bit code at 0x08 and 32-bit code at 0x18")
require("0x00AF9A000000FFFF" in multiboot,
        "Multiboot bootstrap retains 64-bit code at selector 0x08")
require("movw %cs, %di" in entry and "call idt_init_with_selector" in entry,
        "early IDT uses the actual current CS selector")
require("mov $0x08, %edi" not in entry,
        "early IDT no longer assumes a hard-coded loader selector")

arch = main.split("static qm_status_t kernel_init_arch(void)", 1)[1].split(
    "static qm_status_t kernel_init_memory(void)", 1)[0]
require(arch.index("init_gdt_and_tss();") < arch.index("idt_init();"),
        "kernel GDT/TSS is active before replacing the early IDT")

# The capped E820 loop must fail rather than accept a silently truncated map.
require(re.search(r"cmpw \$64, e820_count\s+jae 5f", qboot) is not None,
        "E820 entry-capacity exhaustion reaches the failure path")
require(re.search(r"(?m)^5:\s*\n\s*stc\s*\n\s*ret", qboot) is not None,
        "E820 capacity/API errors return carry to the boot caller")

print("QBoot GDT/early-IDT selector tests: PASS")
'''

# ---------------------------------------------------------------------------
# 7. Extend VFS host test to cover ancestor cycles and repeated mount roots.
# ---------------------------------------------------------------------------
p = "tests/filesystem/vfs_host_test.c"
s = read(p)
s = replace_once(
    s,
    "    CHECK(vfs_create(&home, \"new.txt\", VFS_NODE_FILE, &found) ==\n          VFS_ERR_NOT_SUPPORTED,\n          \"unsupported create operation returns explicit error\");\n\n    printf(\"VFS HOST TESTS PASSED\\n\");\n",
    "    CHECK(vfs_create(&home, \"new.txt\", VFS_NODE_FILE, &found) ==\n          VFS_ERR_NOT_SUPPORTED,\n          \"unsupported create operation returns explicit error\");\n\n    {\n        static struct vfs_node cycle_outer = {\n            .name = \"cycle-outer\", .type = VFS_NODE_DIRECTORY\n        };\n        static struct vfs_node cycle_inner = {\n            .name = \"cycle-inner\", .type = VFS_NODE_DIRECTORY\n        };\n        static struct vfs_node mount_root = {\n            .name = \"mount-root\", .type = VFS_NODE_DIRECTORY\n        };\n        static struct vfs_node mount_child = {\n            .name = \"mount-child\", .type = VFS_NODE_DIRECTORY\n        };\n        static struct vfs_node independent_mountpoint = {\n            .name = \"independent-mount\", .type = VFS_NODE_DIRECTORY\n        };\n\n        CHECK(vfs_add_child(&cycle_outer, &cycle_inner) == VFS_OK,\n              \"detached subtree can be assembled\");\n        CHECK(vfs_add_child(&cycle_inner, &cycle_outer) == VFS_ERR_INVALID,\n              \"ancestor cannot be attached below its descendant\");\n\n        CHECK(vfs_add_child(&mount_root, &mount_child) == VFS_OK,\n              \"detached mount tree can be assembled\");\n        CHECK(vfs_mount(\"ramfs\", &mount_child, &mount_root) == VFS_ERR_INVALID,\n              \"mounting a root below its own descendant is rejected\");\n        CHECK(vfs_mount(\"ramfs\", &independent_mountpoint, &mount_root) == VFS_OK,\n              \"detached filesystem root can be mounted once\");\n        CHECK(vfs_mount(\"ramfs\", &mnt, &mount_root) == VFS_ERR_BUSY,\n              \"already attached filesystem root cannot be mounted twice\");\n    }\n\n    printf(\"VFS HOST TESTS PASSED\\n\");\n",
    "VFS topology regression tests",
)
updates[p] = s

# ---------------------------------------------------------------------------
# 8. Wire the missing PMM tests and selector checks into QBuild/CI.
# ---------------------------------------------------------------------------
p = "Makefile"
s = read(p)
s = replace_once(
    s,
    "HOST_MEMORY_TEST := .memory-host-test\n",
    "HOST_MEMORY_TEST := .memory-host-test\nHOST_MEMORY_OVERLAP_TEST := build/tests/memory/memory_overlap_test\nHOST_MEMORY_BOOT_INFO_TEST := build/tests/memory/memory_boot_info_test\n",
    "PMM host test targets",
)
s = replace_once(
    s,
    "\trm -rf build/tests/kernel-foundation\n",
    "\trm -rf build/tests/kernel-foundation\n\trm -rf build/tests/memory\n",
    "clean PMM regression test binaries",
)
s = replace_once(
    s,
    "test-memory: $(HOST_MEMORY_TEST)\n\t./$(HOST_MEMORY_TEST)\n\n$(HOST_MEMORY_TEST): tests/memory_host_test.c mm/physical/memory.c mm/physical/memory.h\n\t$(HOST_CC) $(HOST_TEST_CFLAGS) -I. -Imm/physical -o $@ tests/memory_host_test.c mm/physical/memory.c\n",
    "test-memory: $(HOST_MEMORY_TEST) $(HOST_MEMORY_OVERLAP_TEST) $(HOST_MEMORY_BOOT_INFO_TEST)\n\t./$(HOST_MEMORY_TEST)\n\t./$(HOST_MEMORY_OVERLAP_TEST)\n\t./$(HOST_MEMORY_BOOT_INFO_TEST)\n\n$(HOST_MEMORY_TEST): tests/memory_host_test.c mm/physical/memory.c mm/physical/memory.h kernel/core/boot_info.c\n\t$(HOST_CC) $(HOST_TEST_CFLAGS) -I. -Iinclude -Imm/physical -o $@ tests/memory_host_test.c mm/physical/memory.c kernel/core/boot_info.c\n\n$(HOST_MEMORY_OVERLAP_TEST): tests/memory_overlap_test.c mm/physical/memory.c mm/physical/memory.h kernel/core/boot_info.c\n\tmkdir -p build/tests/memory\n\t$(HOST_CC) $(HOST_TEST_CFLAGS) -I. -Iinclude -Imm/physical -o $@ tests/memory_overlap_test.c mm/physical/memory.c kernel/core/boot_info.c\n\n$(HOST_MEMORY_BOOT_INFO_TEST): tests/memory_boot_info_test.c mm/physical/memory.c mm/physical/memory.h kernel/core/boot_info.c\n\tmkdir -p build/tests/memory\n\t$(HOST_CC) $(HOST_TEST_CFLAGS) -I. -Iinclude -Imm/physical -o $@ tests/memory_boot_info_test.c mm/physical/memory.c kernel/core/boot_info.c\n",
    "PMM tests are compiled and executed by test-memory",
)
s = replace_once(
    s,
    "\tpython3 tests/boot/test_bios_nxe.py\n\tbash tools/verify_boot.sh\n",
    "\tpython3 tests/boot/test_bios_nxe.py\n\tpython3 tests/boot/test_qboot_gdt.py\n\tbash tools/verify_boot.sh\n",
    "QBoot selector test wired into test-boot",
)
updates[p] = s

# Ensure all modified/new files can be written, then back up and apply.
for path, content in updates.items():
    target = ROOT / path
    if not target.is_file():
        fail(f"Modified target disappeared: {path}")
    if not content.endswith("\n"):
        content += "\n"
        updates[path] = content
for path, content in new_files.items():
    if not content.endswith("\n"):
        new_files[path] = content + "\n"

# Final structural sanity checks before touching the working tree.
require_text = {
    "kernel/core/main.c": ["mm_init_from_boot_info(", "g_boot_multiboot_mmap_length"],
    "mm/physical/memory.c": ["mm_parse_boot_regions", "int mm_init_from_boot_info("],
    "fs/vfs/vfs.c": ["parent_chain_contains", "filesystem_root->parent"],
    "boot/bios/qboot_stage2.S": ["ljmpl $0x18, $protected_entry", "ljmp $0x08, $long_mode_entry"],
}
for path, needles in require_text.items():
    content = updates[path]
    for needle in needles:
        if needle not in content:
            fail(f"Post-patch sanity check failed: {path} missing {needle!r}")

# Backups preserve exact pre-patch contents. If any write fails, restore all
# original files and remove any newly created test/script files.
BACKUP.mkdir(parents=True, exist_ok=False)
written: list[str] = []
created: list[str] = []
try:
    for path in updates:
        backup_path = BACKUP / path
        backup_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / path, backup_path)
    for path, content in updates.items():
        target = ROOT / path
        temp = target.with_name(target.name + ".phase2-repair.tmp")
        temp.write_text(content, encoding="utf-8")
        os.replace(temp, target)
        written.append(path)
    for path, content in new_files.items():
        target = ROOT / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding="utf-8")
        created.append(path)
    MARKER.parent.mkdir(parents=True, exist_ok=True)
    MARKER.write_text(
        "Phase 2 final repair tranche applied locally.\n"
        f"Modified files: {len(updates)}\nNew files: {len(new_files)}\n",
        encoding="utf-8",
    )
except Exception:
    for path in reversed(written):
        shutil.copy2(BACKUP / path, ROOT / path)
    for path in reversed(created):
        try:
            (ROOT / path).unlink()
        except FileNotFoundError:
            pass
    try:
        MARKER.unlink()
    except FileNotFoundError:
        pass
    raise

print("[PHASE 2 REPAIR] Local source modifications applied.")
print(f"[PHASE 2 REPAIR] Modified files: {len(updates)}")
print(f"[PHASE 2 REPAIR] New files: {len(new_files)}")
print(f"[PHASE 2 REPAIR] Backups: {BACKUP.relative_to(ROOT)}")
print("[PHASE 2 REPAIR] No commit or push was performed.")
print("\nNext validation commands:")
print("  python3 -m py_compile tests/boot/test_qboot_gdt.py")
print("  bash tools/ci/check_sources.sh")
print("  ./qbuild test --arch x86_64 --boot bios --jobs 2")
print("  bash tools/ci/run_host_sanitizers.sh")
print("  git diff --check")
