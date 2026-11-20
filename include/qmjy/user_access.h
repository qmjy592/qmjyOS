#ifndef QMJY_USER_ACCESS_H
#define QMJY_USER_ACCESS_H

#include "paging.h"
#include "qmjy/types.h"
#include "qmjy/error.h"

/* Validate every page in a user range before any kernel-side dereference. */
qm_status_t qm_user_range_check(const vm_address_space_t *space,
                                qm_u64_t user_address,
                                qm_size_t length,
                                int write_access);

/* Copy helpers require the supplied address space to be the active CR3. */
qm_status_t qm_copy_from_user(const vm_address_space_t *space,
                              void *kernel_destination,
                              qm_u64_t user_source,
                              qm_size_t length);
qm_status_t qm_copy_to_user(const vm_address_space_t *space,
                            qm_u64_t user_destination,
                            const void *kernel_source,
                            qm_size_t length);

#endif
