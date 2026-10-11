.section .text.boot,"ax"
.global _start64
.type _start64, @function

_start64:
    cli
    lea stack_top64(%rip), %rsp
    xor %rbp, %rbp
    and $-16, %rsp
    call main

1:
    cli
    hlt
    jmp 1b

.size _start64, .-_start64

.section .bss
.align 16
stack_bottom64:
    .skip 16384
stack_top64:

.section .note.GNU-stack,"",@progbits
