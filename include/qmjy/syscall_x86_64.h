#ifndef QMJY_SYSCALL_X86_64_H
#define QMJY_SYSCALL_X86_64_H

#include "qmjy/syscall.h"
#include "qmjy/x86_64_trap.h"

qm_status_t qm_syscall_context_from_x86_64_frame(
    const qm_x86_64_trap_frame_t *frame,
    qm_syscall_context_t *context);

#endif
