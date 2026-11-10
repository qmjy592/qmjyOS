.section .text
.code64
.extern irq_handler

.macro IRQ_NOERR num
.global irq\num
irq\num:
    pushq $0
    pushq $\num  ; 直接压入 0 和 1，不再加 32
    jmp irq_common
.endm

IRQ_NOERR 0
IRQ_NOERR 1

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
    call irq_handler

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
