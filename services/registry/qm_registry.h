#ifndef QM_REGISTRY_H
#define QM_REGISTRY_H

#include <stddef.h>
#include <stdint.h>

#define QM_REGISTRY_MAX 64
#define QM_REGISTRY_NAME_MAX 48

typedef uint32_t qm_registry_endpoint_t;

typedef enum {
    QM_REGISTRY_OK = 0,
    QM_REGISTRY_ERR_INVALID = -1,
    QM_REGISTRY_ERR_FULL = -2,
    QM_REGISTRY_ERR_EXISTS = -3,
    QM_REGISTRY_ERR_NOT_FOUND = -4
} qm_registry_error_t;

void qm_registry_init(void);

int qm_registry_register(
    const char *name,
    qm_registry_endpoint_t endpoint
);

int qm_registry_unregister(const char *name);

int qm_registry_lookup(
    const char *name,
    qm_registry_endpoint_t *endpoint
);

size_t qm_registry_count(void);

#endif
