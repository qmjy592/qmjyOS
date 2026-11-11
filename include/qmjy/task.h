#ifndef QMJY_TASK_H
#define QMJY_TASK_H

#include "types.h"
#include "error.h"

typedef qm_u64_t qm_task_id_t;
typedef qm_u64_t qm_process_id_t;
typedef qm_u64_t qm_thread_id_t;

typedef enum qm_task_state {
    QM_TASK_NEW = 0,
    QM_TASK_READY,
    QM_TASK_RUNNING,
    QM_TASK_BLOCKED,
    QM_TASK_EXITED,
    QM_TASK_ZOMBIE
} qm_task_state_t;

typedef struct qm_task_info {
    qm_task_id_t id;
    qm_task_state_t state;
    qm_u32_t priority;
    qm_u32_t flags;
    qm_u64_t runtime_ns;
} qm_task_info_t;

typedef void (*qm_thread_entry_t)(void *argument);

qm_status_t qm_process_create(
    const char *name,
    qm_process_id_t *out_id
);

qm_status_t qm_process_destroy(qm_process_id_t id);

qm_status_t qm_thread_create(
    qm_process_id_t process,
    qm_thread_entry_t entry,
    void *argument,
    qm_u32_t priority,
    qm_thread_id_t *out_id
);

qm_status_t qm_thread_exit(int exit_code);

qm_status_t qm_scheduler_start(void);
qm_status_t qm_scheduler_yield(void);

qm_status_t qm_scheduler_block(qm_task_id_t task);
qm_status_t qm_scheduler_wake(qm_task_id_t task);

qm_status_t qm_task_query(
    qm_task_id_t id,
    qm_task_info_t *info
);

#endif
