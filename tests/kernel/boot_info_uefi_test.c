#include "qmjy/boot.h"
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
