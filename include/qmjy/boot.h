#ifndef QMJY_BOOT_H
#define QMJY_BOOT_H

#include "types.h"
#include "error.h"

#define QM_BOOT_INFO_MAGIC       0x514D4A59424F4F54ULL
#define QM_BOOT_PROTOCOL_MAJOR   1U
#define QM_BOOT_PROTOCOL_MINOR   1U

typedef enum qm_boot_arch {
    QM_BOOT_ARCH_X86_64 = 1,
    QM_BOOT_ARCH_AARCH64 = 2
} qm_boot_arch_t;

typedef enum qm_boot_firmware {
    QM_BOOT_FIRMWARE_BIOS = 1,
    QM_BOOT_FIRMWARE_UEFI = 2,
    QM_BOOT_FIRMWARE_EFI_STUB = 3
} qm_boot_firmware_t;

typedef enum qm_memory_region_type {
    QM_MEMORY_USABLE = 1,
    QM_MEMORY_RESERVED = 2,
    QM_MEMORY_ACPI_RECLAIM = 3,
    QM_MEMORY_ACPI_NVS = 4,
    QM_MEMORY_BAD = 5,
    QM_MEMORY_BOOTLOADER_RECLAIM = 6,
    QM_MEMORY_KERNEL = 7,
    QM_MEMORY_MODULE = 8,
    QM_MEMORY_FRAMEBUFFER = 9
} qm_memory_region_type_t;

typedef struct qm_boot_memory_region {
    qm_u64_t base;
    qm_u64_t length;
    qm_u32_t type;
    qm_u32_t attributes;
} qm_boot_memory_region_t;

typedef struct qm_boot_module {
    qm_u64_t physical_start;
    qm_u64_t size;
    qm_u64_t name_physical;
    qm_u32_t flags;
    qm_u32_t reserved;
} qm_boot_module_t;

typedef struct qm_boot_framebuffer {
    qm_u64_t physical_address;
    qm_u64_t size;
    qm_u32_t width;
    qm_u32_t height;
    qm_u32_t pitch;
    qm_u16_t bits_per_pixel;
    qm_u16_t pixel_format;
} qm_boot_framebuffer_t;

typedef struct qm_boot_info {
    qm_u64_t magic;

    qm_u16_t protocol_major;
    qm_u16_t protocol_minor;
    qm_u32_t struct_size;

    qm_u32_t architecture;
    qm_u32_t firmware;
    qm_u64_t flags;

    qm_u64_t memory_map_physical;
    qm_u32_t memory_map_count;
    qm_u32_t memory_map_entry_size;

    qm_u64_t modules_physical;
    qm_u32_t module_count;
    qm_u32_t reserved0;

    qm_u64_t command_line_physical;

    qm_boot_framebuffer_t framebuffer;

    qm_u64_t acpi_rsdp_physical;
    qm_u64_t firmware_system_table_physical;

    qm_u64_t kernel_physical_base;
    qm_u64_t kernel_physical_size;
    qm_u64_t kernel_virtual_base;
    qm_u64_t kernel_entry_virtual;

    /* v1.1: active kernel page-table root; zero for legacy BIOS bridge. */
    qm_u64_t page_table_root_physical;

    qm_u64_t reserved[8];
} qm_boot_info_t;


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

#define QM_BOOT_MULTIBOOT_V1_MAGIC 0x2BADB002U
#define QM_BOOT_MAX_MEMORY_REGIONS 128U

/* QBoot UEFI bootstrap allocates one contiguous 68-page paging block.
 * Keep this allocation reserved while it is the active CR3 hierarchy. */
#define QM_BOOT_UEFI_PAGE_TABLE_PAGES 68U
#define QM_BOOT_UEFI_PAGE_SIZE 4096ULL
#define QM_BOOT_UEFI_PAGE_TABLE_SIZE \
    ((qm_u64_t)QM_BOOT_UEFI_PAGE_TABLE_PAGES * QM_BOOT_UEFI_PAGE_SIZE)

qm_status_t qm_boot_validate_info(const qm_boot_info_t *info);

/*
 * Compatibility adapter for the current x86_64 BIOS Multiboot v1 bootstrap.
 * Views may be host pointers while their corresponding physical addresses
 * remain explicit; the kernel caller supplies identity-mapped views.
 * Multiboot modules are rejected until a module descriptor array is copied.
 */
qm_status_t qm_boot_info_from_multiboot_v1(
    qm_u32_t multiboot_magic,
    const void *multiboot_info_view,
    qm_u64_t multiboot_info_physical,
    const void *memory_map_view,
    qm_u64_t memory_map_physical,
    qm_boot_memory_region_t *region_storage,
    qm_u64_t region_storage_physical,
    qm_u32_t region_capacity,
    qm_u64_t kernel_physical_base,
    qm_u64_t kernel_physical_size,
    qm_u64_t kernel_virtual_base,
    qm_u64_t kernel_entry_virtual,
    qm_boot_info_t *out_info
);


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

#endif
