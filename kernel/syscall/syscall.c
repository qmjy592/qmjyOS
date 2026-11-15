#include "qmjy/syscall.h"
#include <stddef.h>

#define QM_SYSCALL_MAX 128U

static qm_syscall_handler_t g_handlers[QM_SYSCALL_MAX];

qm_status_t qm_syscall_register(qm_syscall_number_t number,
                                qm_syscall_handler_t handler)
{
    if (handler == NULL)
        return QM_ERR_INVALID;
    if (number >= QM_SYSCALL_MAX)
        return QM_ERR_RANGE;
    if (g_handlers[number] != NULL)
        return QM_ERR_EXISTS;
    /* Registration is an early-init operation, before user mode is enabled. */
    g_handlers[number] = handler;
    return QM_OK;
}

qm_syscall_result_t qm_syscall_dispatch(const qm_syscall_context_t *context)
{
    qm_syscall_handler_t handler;
    if (context == NULL)
        return (qm_syscall_result_t)QM_ERR_INVALID;
    if (context->number >= QM_SYSCALL_MAX)
        return (qm_syscall_result_t)QM_ERR_RANGE;
    handler = g_handlers[context->number];
    if (handler == NULL)
        return (qm_syscall_result_t)QM_ERR_NOT_SUPPORTED;
    /* Handler-specific code must validate/copy every user pointer. */
    return handler(context);
}
