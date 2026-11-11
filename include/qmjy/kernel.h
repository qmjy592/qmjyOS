#ifndef QMJY_KERNEL_H
#define QMJY_KERNEL_H

#include "types.h"
#include "error.h"
#include "boot.h"

typedef enum qm_init_level {
    QM_INIT_EARLY = 0,
    QM_INIT_ARCH = 1,
    QM_INIT_MEMORY = 2,
    QM_INIT_CORE = 3,
    QM_INIT_DRIVER = 4,
    QM_INIT_FILESYSTEM = 5,
    QM_INIT_LATE = 6
} qm_init_level_t;

typedef qm_status_t (*qm_init_fn_t)(void);

typedef struct qm_init_entry {
    const char *name;
    qm_init_level_t level;
    qm_init_fn_t initialize;
} qm_init_entry_t;

qm_status_t qm_kernel_init(const qm_boot_info_t *boot_info);

qm_status_t qm_init_register(const qm_init_entry_t *entry);
qm_status_t qm_init_run_level(qm_init_level_t level);

void qm_kernel_shutdown(int reason);

#endif
