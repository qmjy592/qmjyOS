#ifndef QM_SERVICE_H
#define QM_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#define QM_SERVICE_MAX          64
#define QM_SERVICE_NAME_MAX     48
#define QM_SERVICE_DEPS_MAX     8

typedef enum {
    QM_SERVICE_EMPTY = 0,
    QM_SERVICE_REGISTERED,
    QM_SERVICE_STARTING,
    QM_SERVICE_RUNNING,
    QM_SERVICE_STOPPING,
    QM_SERVICE_STOPPED,
    QM_SERVICE_FAILED
} qm_service_state_t;

typedef enum {
    QM_SERVICE_OK = 0,
    QM_SERVICE_ERR_INVALID = -1,
    QM_SERVICE_ERR_FULL = -2,
    QM_SERVICE_ERR_DUPLICATE = -3,
    QM_SERVICE_ERR_NOT_FOUND = -4,
    QM_SERVICE_ERR_STATE = -5,
    QM_SERVICE_ERR_DEPENDENCY = -6,
    QM_SERVICE_ERR_CALLBACK = -7,
    QM_SERVICE_ERR_CYCLE = -8,
    QM_SERVICE_ERR_IN_USE = -9
} qm_service_error_t;

typedef int (*qm_service_start_fn)(void);
typedef int (*qm_service_stop_fn)(void);

typedef struct {
    char name[QM_SERVICE_NAME_MAX];
    qm_service_start_fn start;
    qm_service_stop_fn stop;

    const char *dependencies[QM_SERVICE_DEPS_MAX];
    uint8_t dependency_count;

    qm_service_state_t state;
    int last_error;
} qm_service_t;

void qm_service_manager_init(void);

int qm_service_register(
    const char *name,
    qm_service_start_fn start,
    qm_service_stop_fn stop,
    const char *const *dependencies,
    uint8_t dependency_count
);

int qm_service_start(const char *name);
int qm_service_stop(const char *name);

int qm_service_start_all(void);
int qm_service_stop_all(void);

int qm_service_get_state(
    const char *name,
    qm_service_state_t *state
);

int qm_service_get_error(
    const char *name,
    int *error
);

size_t qm_service_count(void);

const char *qm_service_state_name(qm_service_state_t state);

#endif
