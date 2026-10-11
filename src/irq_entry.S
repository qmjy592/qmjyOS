.section .text
.code64
.extern irq_handler

.global irq0
irq0:
    pushq $0
    pushq $32
    jmp irq_common

.global irq1
irq1:
    pushq $0
    pushq $33
    jmp irq_common

irq_common:
    push %rax
    push %rbx
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %rbp
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15

    mov 120(%rsp), %rdi
    # Preserve original interrupt frame and align stack for the C ABI.
    mov %rsp, %r12
    andq $-16, %rsp
    call irq_handler
    mov %r12, %rsp

    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rbp
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %rbx
    pop %rax
    add $16, %rsp
    iretq
