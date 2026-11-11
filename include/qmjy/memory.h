#ifndef QMJY_MEMORY_H
#define QMJY_MEMORY_H

#include "types.h"
#include "error.h"
#include "limits.h"
#include "boot.h"

/*
 * Compatibility aliases.
 * The canonical page-size definitions live in limits.h.
 */
#define QM_PAGE_SIZE  QMjy_PAGE_SIZE
#define QM_PAGE_SHIFT QMjy_PAGE_SHIFT

typedef enum qm_page_flags {
    QM_PAGE_READ    = 1U << 0,
    QM_PAGE_WRITE   = 1U << 1,
    QM_PAGE_EXECUTE = 1U << 2,
    QM_PAGE_USER    = 1U << 3,
    QM_PAGE_GLOBAL  = 1U << 4,
    QM_PAGE_DEVICE  = 1U << 5
} qm_page_flags_t;

typedef struct qm_memory_stats {
    qm_u64_t total_pages;
    qm_u64_t free_pages;
    qm_u64_t reserved_pages;
    qm_u64_t allocated_pages;
} qm_memory_stats_t;

qm_status_t qm_memory_init(
    const qm_boot_info_t *boot_info
);

qm_status_t qm_page_alloc(
    qm_size_t count,
    qm_phys_addr_t *first_page
);

qm_status_t qm_page_free(
    qm_phys_addr_t first_page,
    qm_size_t count
);

qm_status_t qm_vm_map(
    qm_uintptr_t virtual_address,
    qm_phys_addr_t physical_address,
    qm_size_t page_count,
    qm_u32_t flags
);

qm_status_t qm_vm_unmap(
    qm_uintptr_t virtual_address,
    qm_size_t page_count
);

qm_status_t qm_vm_query(
    qm_uintptr_t virtual_address,
    qm_phys_addr_t *physical_address,
    qm_u32_t *flags
);

qm_status_t qm_memory_get_stats(
    qm_memory_stats_t *stats
);

#endif
