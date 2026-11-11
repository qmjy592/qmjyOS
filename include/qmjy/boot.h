#ifndef QMJY_BOOT_H
#define QMJY_BOOT_H

#include "types.h"
#include "error.h"

#define QM_BOOT_INFO_MAGIC       0x514D4A59424F4F54ULL
#define QM_BOOT_PROTOCOL_MAJOR   1U
#define QM_BOOT_PROTOCOL_MINOR   0U

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

    qm_u64_t reserved[8];
} qm_boot_info_t;

qm_status_t qm_boot_validate_info(const qm_boot_info_t *info);

#endif
