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

    # QFS debug: reached 64-bit kernel entry.
    movw $0x00e9, %dx
    movb $'Q', %al
    outb %al, %dx
    movb $'F', %al
    outb %al, %dx
    movb $'S', %al
    outb %al, %dx
    movb $'_', %al
    outb %al, %dx
    movb $'E', %al
    outb %al, %dx
    movb $'N', %al
    outb %al, %dx
    movb $'T', %al
    outb %al, %dx
    movb $'R', %al
    outb %al, %dx
    movb $'Y', %al
    outb %al, %dx
    movb $'6', %al
    outb %al, %dx
    movb $'4', %al
    outb %al, %dx
    movb $'_', %al
    outb %al, %dx
    movb $'S', %al
    outb %al, %dx
    movb $'T', %al
    outb %al, %dx
    movb $'A', %al
    outb %al, %dx
    movb $'R', %al
    outb %al, %dx
    movb $'T', %al
    outb %al, %dx
    movb $10, %al
    outb %al, %dx

    # Set the stack before clearing .bss.
    lea stack_top64(%rip), %rsp
    and $-16, %rsp
    xor %rbp, %rbp

    movw $0x00e9, %dx
    movb $'S', %al
    outb %al, %dx

    # Clear the 64-bit kernel .bss.
    lea __bss_start(%rip), %rdi
    lea __bss_end(%rip), %rcx
    sub %rdi, %rcx

    movw $0x00e9, %dx
    movb $'R', %al
    outb %al, %dx

    xor %eax, %eax

    movw $0x00e9, %dx
    movb $'B', %al
    outb %al, %dx

    xor %eax, %eax
    rep stosb

    # QFS debug: BSS clearing completed.
    movw $0x00e9, %dx
    movb $'D', %al
    outb %al, %dx

    # Restore arguments and call main(magic, info_addr).
    mov %r12d, %edi
    mov %r13d, %esi

    # QFS debug: immediately before main().
    movw $0x00e9, %dx
    movb $'M', %al
    outb %al, %dx

    # Restore the second argument after using RSI as a string pointer.
    mov %r13d, %esi
    call main

    # QFS debug: main unexpectedly returned.
    movw $0x00e9, %dx
    movb $'X', %al
    outb %al, %dx

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
