#include "qmjy/syscall_x86_64.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    qm_x86_64_trap_frame_t frame = {0};
    qm_syscall_context_t context = {0};

    frame.cs = 0x23;
    frame.rax = 0x55;
    frame.rdi = 0x101;
    frame.rsi = 0x202;
    frame.rdx = 0x303;
    frame.r10 = 0x404;
    frame.r8 = 0x505;
    frame.r9 = 0x606;
    frame.rip = 0x40001234;
    frame.user_rsp = 0x7fffffffd000;
    frame.rflags = 0x202;

    assert(qm_syscall_context_from_x86_64_frame(&frame, &context) == QM_OK);
    assert(context.number == 0x55);
    assert(context.arguments[0] == 0x101);
    assert(context.arguments[1] == 0x202);
    assert(context.arguments[2] == 0x303);
    assert(context.arguments[3] == 0x404);
    assert(context.arguments[4] == 0x505);
    assert(context.arguments[5] == 0x606);
    assert(context.user_instruction_pointer == 0x40001234);
    assert(context.user_stack_pointer == 0x7fffffffd000);
    assert(context.flags == 0x202);

    frame.cs = 0x08;
    assert(qm_syscall_context_from_x86_64_frame(&frame, &context) ==
           QM_ERR_PERMISSION);
    assert(qm_syscall_context_from_x86_64_frame(NULL, &context) ==
           QM_ERR_INVALID);
    assert(qm_syscall_context_from_x86_64_frame(&frame, NULL) ==
           QM_ERR_INVALID);

    puts("x86_64 syscall trap-frame ABI tests: PASS");
    return 0;
}
