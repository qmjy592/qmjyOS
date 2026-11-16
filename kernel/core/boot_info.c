#include "qmjy/boot.h"
#include <stddef.h>
#include <stdint.h>

static int range_overflows(uint64_t base, uint64_t length)
{
    return length != 0 && base > UINT64_MAX - length;
}

static int product_overflows(uint64_t count, uint64_t element_size)
{
    return element_size != 0 && count > UINT64_MAX / element_size;
}

qm_status_t qm_boot_validate_info(const qm_boot_info_t *info)
{
    uint64_t map_bytes;
    uint64_t module_bytes;

    if (info == NULL)
        return QM_ERR_INVALID;

    if (info->magic != QM_BOOT_INFO_MAGIC)
        return QM_ERR_CORRUPT;

    if (info->protocol_major != QM_BOOT_PROTOCOL_MAJOR ||
        info->protocol_minor > QM_BOOT_PROTOCOL_MINOR)
        return QM_ERR_NOT_SUPPORTED;

    /* All fields consumed below must be present in this protocol structure. */
    if (info->struct_size < offsetof(qm_boot_info_t, reserved))
        return QM_ERR_CORRUPT;

    if (info->architecture != QM_BOOT_ARCH_X86_64 &&
        info->architecture != QM_BOOT_ARCH_AARCH64)
        return QM_ERR_NOT_SUPPORTED;

    if (info->firmware != QM_BOOT_FIRMWARE_BIOS &&
        info->firmware != QM_BOOT_FIRMWARE_UEFI &&
        info->firmware != QM_BOOT_FIRMWARE_EFI_STUB)
        return QM_ERR_NOT_SUPPORTED;

    if (info->memory_map_count == 0 || info->memory_map_physical == 0 ||
        info->memory_map_entry_size < sizeof(qm_boot_memory_region_t))
        return QM_ERR_CORRUPT;

    if (product_overflows(info->memory_map_count,
                          info->memory_map_entry_size))
        return QM_ERR_OVERFLOW;
    map_bytes = (uint64_t)info->memory_map_count *
                (uint64_t)info->memory_map_entry_size;
    if (range_overflows(info->memory_map_physical, map_bytes))
        return QM_ERR_OVERFLOW;

    if (info->module_count != 0) {
        if (info->modules_physical == 0)
            return QM_ERR_CORRUPT;
        if (product_overflows(info->module_count, sizeof(qm_boot_module_t)))
            return QM_ERR_OVERFLOW;
        module_bytes = (uint64_t)info->module_count * sizeof(qm_boot_module_t);
        if (range_overflows(info->modules_physical, module_bytes))
            return QM_ERR_OVERFLOW;
    }

    if (info->kernel_physical_size == 0 ||
        range_overflows(info->kernel_physical_base,
                        info->kernel_physical_size) ||
        info->kernel_entry_virtual == 0)
        return QM_ERR_CORRUPT;

    if ((info->framebuffer.physical_address == 0) !=
        (info->framebuffer.size == 0))
        return QM_ERR_CORRUPT;
    if (info->framebuffer.size != 0 &&
        range_overflows(info->framebuffer.physical_address,
                        info->framebuffer.size))
        return QM_ERR_OVERFLOW;

    return QM_OK;
}


#define QM_MB1_INFO_MIN_SIZE 88U
#define QM_BOOT_IDENTITY_LIMIT 0x40000000ULL

static qm_u32_t read_le32(const qm_u8_t *p)
{
    return (qm_u32_t)p[0] |
           ((qm_u32_t)p[1] << 8) |
           ((qm_u32_t)p[2] << 16) |
           ((qm_u32_t)p[3] << 24);
}

static qm_u64_t read_le64(const qm_u8_t *p)
{
    return (qm_u64_t)read_le32(p) |
           ((qm_u64_t)read_le32(p + 4) << 32);
}

static qm_u32_t qm_boot_map_e820_type(qm_u32_t type)
{
    switch (type) {
    case 1: return QM_MEMORY_USABLE;
    case 3: return QM_MEMORY_ACPI_RECLAIM;
    case 4: return QM_MEMORY_ACPI_NVS;
    case 5: return QM_MEMORY_BAD;
    default: return QM_MEMORY_RESERVED;
    }
}

static int qm_boot_ranges_overlap(qm_u64_t a_base, qm_u64_t a_length,
                                  qm_u64_t b_base, qm_u64_t b_length)
{
    qm_u64_t a_end;
    qm_u64_t b_end;
    if (a_length == 0 || b_length == 0 ||
        range_overflows(a_base, a_length) ||
        range_overflows(b_base, b_length))
        return 1;
    a_end = a_base + a_length;
    b_end = b_base + b_length;
    return a_base < b_end && b_base < a_end;
}

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
    qm_boot_info_t *out_info)
{
    const qm_u8_t *mb = (const qm_u8_t *)multiboot_info_view;
    const qm_u8_t *map = (const qm_u8_t *)memory_map_view;
    qm_boot_memory_region_t parsed[QM_BOOT_MAX_MEMORY_REGIONS];
    qm_boot_info_t result;
    qm_u32_t flags;
    qm_u32_t map_length;
    qm_u32_t map_address;
    qm_u32_t module_count;
    qm_u32_t cursor = 0;
    qm_u32_t count = 0;
    qm_u64_t storage_bytes;
    qm_u64_t virtual_end;
    qm_size_t i;

    if (multiboot_magic != QM_BOOT_MULTIBOOT_V1_MAGIC)
        return QM_ERR_CORRUPT;
    if (!mb || !map || !region_storage || !out_info)
        return QM_ERR_INVALID;
    if (multiboot_info_physical == 0 ||
        multiboot_info_physical > QM_BOOT_IDENTITY_LIMIT - QM_MB1_INFO_MIN_SIZE)
        return QM_ERR_RANGE;
    if (region_capacity == 0 || region_capacity > QM_BOOT_MAX_MEMORY_REGIONS)
        return QM_ERR_RANGE;
    if (memory_map_physical == 0 || memory_map_physical >= QM_BOOT_IDENTITY_LIMIT)
        return QM_ERR_RANGE;

    flags = read_le32(mb);
    if ((flags & (1U << 6)) == 0)
        return QM_ERR_CORRUPT;
    module_count = read_le32(mb + 20);
    if ((flags & (1U << 3)) != 0 && module_count != 0)
        return QM_ERR_NOT_SUPPORTED;

    map_length = read_le32(mb + 44);
    map_address = read_le32(mb + 48);
    if (map_length == 0 || map_address == 0 ||
        (qm_u64_t)map_address != memory_map_physical)
        return QM_ERR_CORRUPT;
    if ((qm_u64_t)map_address + map_length > QM_BOOT_IDENTITY_LIMIT)
        return QM_ERR_RANGE;
    if (product_overflows(region_capacity, sizeof(*region_storage)))
        return QM_ERR_OVERFLOW;
    storage_bytes = (qm_u64_t)region_capacity * sizeof(*region_storage);
    if (region_storage_physical == 0 ||
        range_overflows(region_storage_physical, storage_bytes) ||
        region_storage_physical + storage_bytes > QM_BOOT_IDENTITY_LIMIT)
        return QM_ERR_RANGE;

    while (cursor < map_length) {
        qm_u32_t payload_size;
        qm_u32_t step;
        qm_u64_t base;
        qm_u64_t length;
        qm_u32_t type;
        qm_u32_t j;

        if (map_length - cursor < 4U)
            return QM_ERR_CORRUPT;
        payload_size = read_le32(map + cursor);
        if (payload_size < 20U || payload_size > map_length - cursor - 4U)
            return QM_ERR_CORRUPT;
        step = payload_size + 4U;
        base = read_le64(map + cursor + 4U);
        length = read_le64(map + cursor + 12U);
        type = read_le32(map + cursor + 20U);
        if (length == 0 || range_overflows(base, length))
            return QM_ERR_CORRUPT;

        if (count >= region_capacity || count >= QM_BOOT_MAX_MEMORY_REGIONS)
            return QM_ERR_NO_SPACE;
        for (j = 0; j < count; ++j) {
            if (qm_boot_ranges_overlap(parsed[j].base, parsed[j].length,
                                       base, length))
                return QM_ERR_CORRUPT;
        }
        parsed[count].base = base;
        parsed[count].length = length;
        parsed[count].type = qm_boot_map_e820_type(type);
        parsed[count].attributes = 1U;
        ++count;
        cursor += step;
    }

    if (cursor != map_length || count == 0)
        return QM_ERR_CORRUPT;
    if (kernel_physical_size == 0 ||
        range_overflows(kernel_physical_base, kernel_physical_size) ||
        kernel_virtual_base > UINT64_MAX - kernel_physical_size)
        return QM_ERR_OVERFLOW;
    virtual_end = kernel_virtual_base + kernel_physical_size;
    if (kernel_entry_virtual < kernel_virtual_base ||
        kernel_entry_virtual >= virtual_end)
        return QM_ERR_CORRUPT;

    for (i = 0; i < sizeof(result); ++i)
        ((qm_u8_t *)&result)[i] = 0;
    result.magic = QM_BOOT_INFO_MAGIC;
    result.protocol_major = QM_BOOT_PROTOCOL_MAJOR;
    result.protocol_minor = QM_BOOT_PROTOCOL_MINOR;
    result.struct_size = (qm_u32_t)sizeof(result);
    result.architecture = QM_BOOT_ARCH_X86_64;
    result.firmware = QM_BOOT_FIRMWARE_BIOS;
    result.memory_map_physical = region_storage_physical;
    result.memory_map_count = count;
    result.memory_map_entry_size = (qm_u32_t)sizeof(*region_storage);
    result.kernel_physical_base = kernel_physical_base;
    result.kernel_physical_size = kernel_physical_size;
    result.kernel_virtual_base = kernel_virtual_base;
    result.kernel_entry_virtual = kernel_entry_virtual;

    if (qm_boot_validate_info(&result) != QM_OK)
        return QM_ERR_CORRUPT;
    for (i = 0; i < count; ++i)
        region_storage[i] = parsed[i];
    *out_info = result;
    return QM_OK;
}
