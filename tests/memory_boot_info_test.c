#define _GNU_SOURCE
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
