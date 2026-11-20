#include "qmjy/syscall_x86_64.h"
#include <stddef.h>

qm_status_t qm_syscall_context_from_x86_64_frame(
    const qm_x86_64_trap_frame_t *frame,
    qm_syscall_context_t *context)
{
    if (frame == NULL || context == NULL)
        return QM_ERR_INVALID;
    if ((frame->cs & 3U) != 3U)
        return QM_ERR_PERMISSION;

    context->number = frame->rax;
    context->arguments[0] = frame->rdi;
    context->arguments[1] = frame->rsi;
    context->arguments[2] = frame->rdx;
    context->arguments[3] = frame->r10;
    context->arguments[4] = frame->r8;
    context->arguments[5] = frame->r9;
    context->user_instruction_pointer = frame->rip;
    context->user_stack_pointer = frame->user_rsp;
    context->flags = frame->rflags;
    return QM_OK;
}
