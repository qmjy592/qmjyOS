#ifndef QMJY_MODULE_H
#define QMJY_MODULE_H

#include "types.h"
#include "error.h"

#define QM_MODULE_ABI_MAJOR 1U
#define QM_MODULE_ABI_MINOR 0U

typedef struct qm_module_descriptor {
    const char *name;

    qm_u16_t abi_major;
    qm_u16_t abi_minor;

    qm_u32_t flags;

    const char *const *dependencies;
    qm_size_t dependency_count;

    qm_status_t (*initialize)(void);
    void (*shutdown)(void);
} qm_module_descriptor_t;

qm_status_t qm_module_register(
    const qm_module_descriptor_t *module
);

qm_status_t qm_module_initialize_all(void);
qm_status_t qm_module_shutdown_all(void);

const qm_module_descriptor_t *qm_module_find(
    const char *name
);

#endif
