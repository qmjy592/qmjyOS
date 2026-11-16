#include "qmjy/boot.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}
static void put64(uint8_t *p, uint64_t value)
{
    put32(p, (uint32_t)value); put32(p + 4, (uint32_t)(value >> 32));
}

int main(void)
{
    uint8_t mbinfo[88] = {0};
    uint8_t mmap[48] = {0};
    qm_boot_memory_region_t regions[4];
    qm_boot_info_t info;
    const uint32_t mmap_phys = 0x80000;

    put32(mbinfo + 0, 1U << 6);
    put32(mbinfo + 44, sizeof(mmap));
    put32(mbinfo + 48, mmap_phys);

    put32(mmap + 0, 20); put64(mmap + 4, 0x100000); put64(mmap + 12, 0x3F00000);
    put32(mmap + 20, 1);
    put32(mmap + 24, 20); put64(mmap + 28, 0x4000000); put64(mmap + 36, 0x100000);
    put32(mmap + 44, 2);

    assert(qm_boot_info_from_multiboot_v1(
        0x2BADB002U, mbinfo, 0x70000, mmap, mmap_phys,
        regions, 0x90000, 4,
        0x200000, 0x180000, 0x200000, 0x200010,
        &info) == QM_OK);
    assert(info.architecture == QM_BOOT_ARCH_X86_64);
    assert(info.firmware == QM_BOOT_FIRMWARE_BIOS);
    assert(info.memory_map_count == 2);
    assert(regions[0].type == QM_MEMORY_USABLE);
    assert(regions[1].type == QM_MEMORY_RESERVED);
    assert(qm_boot_validate_info(&info) == QM_OK);

    assert(qm_boot_info_from_multiboot_v1(
        0, mbinfo, 0x70000, mmap, mmap_phys, regions, 0x90000, 4,
        0x200000, 0x180000, 0x200000, 0x200010, &info) == QM_ERR_CORRUPT);

    put32(mmap + 0, 2);
    assert(qm_boot_info_from_multiboot_v1(
        0x2BADB002U, mbinfo, 0x70000, mmap, mmap_phys, regions, 0x90000, 4,
        0x200000, 0x180000, 0x200000, 0x200010, &info) == QM_ERR_CORRUPT);

    /* A region-capacity failure must not publish a partial output array. */
    put32(mmap + 0, 20);
    regions[0].base = 0xDEADBEEFULL;
    assert(qm_boot_info_from_multiboot_v1(
        0x2BADB002U, mbinfo, 0x70000, mmap, mmap_phys, regions, 0x90000, 1,
        0x200000, 0x180000, 0x200000, 0x200010, &info) == QM_ERR_NO_SPACE);
    assert(regions[0].base == 0xDEADBEEFULL);

    /* Overlapping E820 ranges are rejected. */
    put64(mmap + 28, 0x3F80000);
    assert(qm_boot_info_from_multiboot_v1(
        0x2BADB002U, mbinfo, 0x70000, mmap, mmap_phys, regions, 0x90000, 4,
        0x200000, 0x180000, 0x200000, 0x200010, &info) == QM_ERR_CORRUPT);
    put64(mmap + 28, 0x4000000);

    /* Multiboot modules are deliberately unsupported until copied/owned. */
    put32(mbinfo + 0, (1U << 6) | (1U << 3));
    put32(mbinfo + 20, 1);
    assert(qm_boot_info_from_multiboot_v1(
        0x2BADB002U, mbinfo, 0x70000, mmap, mmap_phys, regions, 0x90000, 4,
        0x200000, 0x180000, 0x200000, 0x200010, &info) == QM_ERR_NOT_SUPPORTED);

    puts("Multiboot v1 to Boot Info adapter tests: PASS");
    return 0;
}
