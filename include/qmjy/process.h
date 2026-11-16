#ifndef QMJY_PROCESS_H
#define QMJY_PROCESS_H

#include "qmjy/types.h"
#include "qmjy/error.h"
#include "elf64.h"

#define QM_PROCESS_MAX 32U
#define QM_PROCESS_NAME_MAX 32U

typedef qm_u64_t qm_process_handle_t;

typedef enum qm_process_state {
    QM_PROCESS_FREE = 0,
    QM_PROCESS_READY,
    QM_PROCESS_RUNNING,
    QM_PROCESS_EXITED
} qm_process_state_t;

typedef struct qm_process_info {
    qm_process_handle_t id;
    qm_process_state_t state;
    qm_s32_t exit_code;
    char name[QM_PROCESS_NAME_MAX];
    qm_elf64_image_t image;
} qm_process_info_t;

/*
 * Bounded early process registry. It owns process metadata and a copy of the
 * ELF image description; the caller remains responsible for mapping lifetime
 * and for providing a real context-switching scheduler.
 */
qm_status_t qm_process_table_init(void);
qm_status_t qm_process_register_image(
    const char *name,
    const qm_elf64_image_t *image,
    qm_process_handle_t *out_id
);
qm_status_t qm_process_mark_running(qm_process_handle_t id);
qm_status_t qm_process_exit(qm_process_handle_t id, qm_s32_t exit_code);
qm_status_t qm_process_reap(qm_process_handle_t id);
qm_status_t qm_process_query(qm_process_handle_t id, qm_process_info_t *out_info);

#endif
