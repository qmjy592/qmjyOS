#ifndef QMJY_SYSCALL_H
#define QMJY_SYSCALL_H

#include "types.h"
#include "error.h"

#define QM_SYSCALL_ABI_MAJOR 1U
#define QM_SYSCALL_ABI_MINOR 0U

/* Reserved test probe: exits the Ring 3 smoke test back to its Ring 0 continuation. */
#define QM_SYSCALL_TEST_EXIT 1U
#define QM_SYSCALL_TEST_ECHO 2U

typedef qm_u64_t qm_syscall_number_t;
typedef qm_s64_t qm_syscall_result_t;

typedef struct qm_syscall_context {
    qm_u64_t number;
    qm_u64_t arguments[6];

    qm_u64_t user_instruction_pointer;
    qm_u64_t user_stack_pointer;

    qm_u64_t flags;
} qm_syscall_context_t;

typedef qm_syscall_result_t (*qm_syscall_handler_t)(
    const qm_syscall_context_t *context
);

qm_status_t qm_syscall_register(
    qm_syscall_number_t number,
    qm_syscall_handler_t handler
);

qm_syscall_result_t qm_syscall_dispatch(
    const qm_syscall_context_t *context
);

#endif
