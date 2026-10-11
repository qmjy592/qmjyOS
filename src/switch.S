.section .text
.code64
.global switch_to
.global task_trampoline
.extern task_exit
# context memory order: r15,r14,r13,r12,rbx,rbp,return RIP
switch_to:
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rsp, (%rdi)
    mov %rsi, %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    ret
# First entry: r12 contains task entry; align stack for SysV call.
task_trampoline:
    mov %r12, %rax
    sub $8, %rsp
    call *%rax
    add $8, %rsp
    call task_exit
1:  cli
    hlt
    jmp 1b
