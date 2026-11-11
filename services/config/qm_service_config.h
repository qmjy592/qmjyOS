#ifndef QM_SERVICE_CONFIG_H
#define QM_SERVICE_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#include "../manager/qm_service.h"

#define QM_SERVICE_CONFIG_MAX QM_SERVICE_MAX

typedef struct {
    const char *name;
    qm_service_start_fn start;
    qm_service_stop_fn stop;
    const char *const *dependencies;
    uint8_t dependency_count;
    int enabled;
} qm_service_config_entry_t;

typedef enum {
    QM_SERVICE_CONFIG_OK = 0,
    QM_SERVICE_CONFIG_ERR_INVALID = -1,
    QM_SERVICE_CONFIG_ERR_FULL = -2,
    QM_SERVICE_CONFIG_ERR_DUPLICATE = -3,
    QM_SERVICE_CONFIG_ERR_DEPENDENCY = -4,
    QM_SERVICE_CONFIG_ERR_CYCLE = -5,
    QM_SERVICE_CONFIG_ERR_STATE = -6,
    QM_SERVICE_CONFIG_ERR_REGISTER = -7
} qm_service_config_error_t;

int qm_service_config_validate(
    const qm_service_config_entry_t *entries,
    size_t count
);

int qm_service_config_apply(
    const qm_service_config_entry_t *entries,
    size_t count
);

#endif
