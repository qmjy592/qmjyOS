#include "qmjy/user_access.h"

#include <stdint.h>

static void copy_bytes(void *destination, const void *source, qm_size_t length)
{
    volatile qm_u8_t *dst = (volatile qm_u8_t *)destination;
    const volatile qm_u8_t *src = (const volatile qm_u8_t *)source;
    qm_size_t i;

    for (i = 0; i < length; ++i)
        dst[i] = src[i];
}

qm_status_t qm_user_range_check(const vm_address_space_t *space,
                                qm_u64_t user_address,
                                qm_size_t length,
                                int write_access)
{
    qm_u64_t end;
    qm_u64_t page;
    qm_u64_t last_page;

    if (space == NULL || write_access < 0 || write_access > 1)
        return QM_ERR_INVALID;
    if (user_address < VM_USER_MIN_VA || user_address >= VM_USER_MAX_VA)
        return QM_ERR_PERMISSION;
    if (length == 0)
        return QM_OK;
    if ((qm_u64_t)length > UINT64_MAX - user_address)
        return QM_ERR_OVERFLOW;
    end = user_address + (qm_u64_t)length;
    if (end <= user_address || end > VM_USER_MAX_VA)
        return QM_ERR_RANGE;

    page = user_address & ~(VM_PAGE_SIZE - 1U);
    last_page = (end - 1U) & ~(VM_PAGE_SIZE - 1U);
    for (;;) {
        qm_u64_t physical;
        qm_u64_t flags;
        int status = vm_address_space_query_page(space, page,
                                                  &physical, &flags);
        (void)physical;
        if (status != VM_OK || !(flags & VM_PRESENT) || !(flags & VM_USER))
            return QM_ERR_PERMISSION;
        if (write_access && !(flags & VM_WRITE))
            return QM_ERR_PERMISSION;
        if (page == last_page)
            break;
        if (page > UINT64_MAX - VM_PAGE_SIZE)
            return QM_ERR_OVERFLOW;
        page += VM_PAGE_SIZE;
    }
    return QM_OK;
}

qm_status_t qm_copy_from_user(const vm_address_space_t *space,
                              void *kernel_destination,
                              qm_u64_t user_source,
                              qm_size_t length)
{
    qm_status_t status;

    if (kernel_destination == NULL && length != 0)
        return QM_ERR_INVALID;
    if (vm_current_root_physical() == 0 || space == NULL ||
        vm_current_root_physical() != space->root_physical)
        return QM_ERR_STATE;
    status = qm_user_range_check(space, user_source, length, 0);
    if (status != QM_OK)
        return status;
    copy_bytes(kernel_destination, (const void *)(uintptr_t)user_source, length);
    return QM_OK;
}

qm_status_t qm_copy_to_user(const vm_address_space_t *space,
                            qm_u64_t user_destination,
                            const void *kernel_source,
                            qm_size_t length)
{
    qm_status_t status;

    if (kernel_source == NULL && length != 0)
        return QM_ERR_INVALID;
    if (vm_current_root_physical() == 0 || space == NULL ||
        vm_current_root_physical() != space->root_physical)
        return QM_ERR_STATE;
    status = qm_user_range_check(space, user_destination, length, 1);
    if (status != QM_OK)
        return status;
    copy_bytes((void *)(uintptr_t)user_destination, kernel_source, length);
    return QM_OK;
}
