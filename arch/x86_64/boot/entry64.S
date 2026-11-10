.section .text.boot,"ax"
.global _start64
.type _start64, @function

.extern __bss_start
.extern __bss_end

_start64:
    # Preserve the 32-bit Multiboot arguments.
    mov %eax, %r12d
    mov %ebx, %r13d

    cli
    cld

    # Set the stack before clearing .bss.
    lea stack_top64(%rip), %rsp
    and $-16, %rsp
    xor %rbp, %rbp

    # Clear the 64-bit kernel .bss.
    lea __bss_start(%rip), %rdi
    lea __bss_end(%rip), %rcx
    sub %rdi, %rcx
    xor %eax, %eax
    rep stosb

    # Restore arguments and call main(magic, info_addr).
    mov %r12d, %edi
    mov %r13d, %esi
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
