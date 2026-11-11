#ifndef QM_INIT_H
#define QM_INIT_H

#include <stddef.h>

#define QM_INIT_MAX_ENTRIES 64
#define QM_INIT_NAME_MAX   48

typedef enum {
    QM_INIT_STAGE_EARLY = 0,
    QM_INIT_STAGE_CORE,
    QM_INIT_STAGE_SERVICES,
    QM_INIT_STAGE_LATE,
    QM_INIT_STAGE_COUNT
} qm_init_stage_t;

typedef enum {
    QM_INIT_OK = 0,
    QM_INIT_ERR_INVALID = -1,
    QM_INIT_ERR_FULL = -2,
    QM_INIT_ERR_DUPLICATE = -3,
    QM_INIT_ERR_NOT_FOUND = -4,
    QM_INIT_ERR_STAGE_ORDER = -5,
    QM_INIT_ERR_STATE = -6,
    QM_INIT_ERR_CALLBACK = -7
} qm_init_error_t;

typedef enum {
    QM_INIT_STATE_PENDING = 0,
    QM_INIT_STATE_RUNNING,
    QM_INIT_STATE_DONE,
    QM_INIT_STATE_FAILED
} qm_init_state_t;

typedef int (*qm_init_callback_t)(void);

typedef struct {
    char name[QM_INIT_NAME_MAX];
    qm_init_callback_t callback;
    qm_init_stage_t stage;
    int priority;
    qm_init_state_t state;
    int callback_result;
} qm_init_entry_t;

void qm_init_init(void);

int qm_init_register(
    const char *name,
    qm_init_stage_t stage,
    int priority,
    qm_init_callback_t callback
);

int qm_init_run_stage(qm_init_stage_t stage);
int qm_init_run_all(void);

int qm_init_get_state(
    const char *name,
    qm_init_state_t *state
);

int qm_init_get_callback_result(
    const char *name,
    int *result
);

size_t qm_init_count(void);
const char *qm_init_stage_name(qm_init_stage_t stage);
const char *qm_init_state_name(qm_init_state_t state);

#endif
