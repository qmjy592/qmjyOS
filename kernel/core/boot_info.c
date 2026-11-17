#include "qmjy/boot.h"
#include <stddef.h>
#include <stdint.h>
#include "qmjy/boot_offsets.h"

_Static_assert(offsetof(qm_boot_info_t, page_table_root_physical) ==
               QM_BOOT_INFO_PAGE_TABLE_ROOT_OFFSET,
               "boot info page-table-root offset changed; update entry64.S");

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
    uint64_t kernel_virtual_end;

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

    if (info->protocol_minor >= 1U &&
        info->firmware == QM_BOOT_FIRMWARE_UEFI &&
        (info->page_table_root_physical == 0 ||
         (info->page_table_root_physical & 0xFFFULL) != 0 ||
         info->page_table_root_physical >= 0x40000000ULL))
        return QM_ERR_CORRUPT;

    /* The active UEFI CR3 block is a single 68-page allocation.
     * Validate the complete span, not only the root page address. */
    if (info->protocol_minor >= 1U &&
        info->firmware == QM_BOOT_FIRMWARE_UEFI &&
        info->page_table_root_physical >
            0x40000000ULL - QM_BOOT_UEFI_PAGE_TABLE_SIZE)
        return QM_ERR_CORRUPT;

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
        info->kernel_entry_virtual == 0)
        return QM_ERR_CORRUPT;

    if (range_overflows(info->kernel_physical_base,
                        info->kernel_physical_size) ||
        range_overflows(info->kernel_virtual_base,
                        info->kernel_physical_size))
        return QM_ERR_OVERFLOW;

    kernel_virtual_end = info->kernel_virtual_base +
                         info->kernel_physical_size;
    if (info->kernel_entry_virtual < info->kernel_virtual_base ||
        info->kernel_entry_virtual >= kernel_virtual_end)
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
