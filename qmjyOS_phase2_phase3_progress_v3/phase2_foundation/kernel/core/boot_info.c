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
