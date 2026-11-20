#ifndef QMJY_X86_64_TRAP_H
#define QMJY_X86_64_TRAP_H

#include "types.h"
#include <stddef.h>

/*
 * Stack image built by arch/x86_64/cpu/ring3_entry.S and interrupts/excep.S.
 * The register/vector/error prefix is identical for a syscall and exception.
 * user_rsp/user_ss are present only when the CPU entered from CPL3.
 */
typedef struct qm_x86_64_trap_frame {
    qm_u64_t r15;
    qm_u64_t r14;
    qm_u64_t r13;
    qm_u64_t r12;
    qm_u64_t r11;
    qm_u64_t r10;
    qm_u64_t r9;
    qm_u64_t r8;
    qm_u64_t rbp;
    qm_u64_t rdi;
    qm_u64_t rsi;
    qm_u64_t rdx;
    qm_u64_t rcx;
    qm_u64_t rbx;
    qm_u64_t rax;
    qm_u64_t vector;
    qm_u64_t error_code;
    qm_u64_t rip;
    qm_u64_t cs;
    qm_u64_t rflags;
    qm_u64_t user_rsp;
    qm_u64_t user_ss;
} qm_x86_64_trap_frame_t;

/* Keep the C view locked to the assembly push order and fixed offsets. */
_Static_assert(offsetof(qm_x86_64_trap_frame_t, r10) == 40,
               "x86_64 trap-frame R10 offset mismatch");
_Static_assert(offsetof(qm_x86_64_trap_frame_t, r9) == 48,
               "x86_64 trap-frame R9 offset mismatch");
_Static_assert(offsetof(qm_x86_64_trap_frame_t, r8) == 56,
               "x86_64 trap-frame R8 offset mismatch");
_Static_assert(offsetof(qm_x86_64_trap_frame_t, rdi) == 72,
               "x86_64 trap-frame RDI offset mismatch");
_Static_assert(offsetof(qm_x86_64_trap_frame_t, rsi) == 80,
               "x86_64 trap-frame RSI offset mismatch");
_Static_assert(offsetof(qm_x86_64_trap_frame_t, rdx) == 88,
               "x86_64 trap-frame RDX offset mismatch");
_Static_assert(offsetof(qm_x86_64_trap_frame_t, rax) == 112,
               "x86_64 trap-frame RAX offset mismatch");
_Static_assert(offsetof(qm_x86_64_trap_frame_t, vector) == 120,
               "x86_64 trap-frame vector offset mismatch");
_Static_assert(offsetof(qm_x86_64_trap_frame_t, error_code) == 128,
               "x86_64 trap-frame error offset mismatch");
_Static_assert(offsetof(qm_x86_64_trap_frame_t, rip) == 136,
               "x86_64 trap-frame RIP offset mismatch");
_Static_assert(offsetof(qm_x86_64_trap_frame_t, user_rsp) == 160,
               "x86_64 trap-frame user RSP offset mismatch");
_Static_assert(sizeof(qm_x86_64_trap_frame_t) == 176,
               "x86_64 trap-frame size mismatch");

/* Reserved internal return value: terminate the current CPL3 activation. */
#define QM_X86_64_TRAP_EXIT_TO_KERNEL 0x8000000000000000ULL

#endif
