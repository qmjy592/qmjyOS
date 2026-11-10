.section .text
.code64

.global switch_to
.global task_trampoline

.extern task_exit

# struct context:
#   0: r15
#   8: r14
#  16: r13
#  24: r12
#  32: rbx
#  40: rbp
#  48: return RIP (位于 context 之后，不属于 struct context)

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

task_trampoline:
    mov %r12, %rax
    sub $8, %rsp
    call *%rax
    call task_exit

1:
    cli
    hlt
    jmp 1b

.section .note.GNU-stack,"",@progbits
