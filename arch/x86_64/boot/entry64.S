.section .text.boot,"ax"
.global _start64
.type _start64, @function

.extern __bss_start
.extern __bss_end
.extern idt_init_with_selector

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

    # .bss is clear; install exception gates before entering C main.
    # Use the selector that actually brought us into 64-bit mode.
    # Both supported BIOS handoffs standardize this as 0x08, but reading CS
    # keeps the early IDT correct if another compatible loader uses a peer GDT.
    xor %edi, %edi
    movw %cs, %di
    call idt_init_with_selector

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


#include "qmjy/boot_offsets.h"

/*
 * UEFI handoff entry. The loader has exited Boot Services and transfers
 * qm_boot_info_t* in RDI using the x86_64 System V kernel ABI. The loader
 * supplies a 1-GiB identity page-table root in the v1.1 boot record. This
 * routine installs a kernel-owned temporary GDT, switches CR3, clears BSS,
 * installs early exception gates, and enters main_uefi().
 */
.section .text.boot.uefi,"ax"
.align 16
.global _start64_uefi
.type _start64_uefi, @function
#ifndef QBOOT_BIOS_ONLY
.extern main_uefi
#endif

_start64_uefi:
    cli
    cld
    mov %rdi, %r12

    # Stop depending on firmware-owned GDT memory before replacing its mappings.
    lgdt uefi_gdt_ptr(%rip)
    pushq $0x08
    leaq .Luefi_kernel_cs(%rip), %rax
    pushq %rax
    lretq
.Luefi_kernel_cs:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss

    # Match the current BIOS bootstrap CPU state. The kernel has not yet
    # implemented SMEP/SMAP/PKE handling; disable these firmware leftovers.
    # Clearing PGE also flushes firmware global TLB entries before CR3 changes.
    mov %cr4, %rax
    test $(1 << 17), %rax
    jz .Luefi_pcide_disabled
    # When PCIDE is active, first reload the current CR3 with PCID=0;
    # Intel requires CR3[11:0]=0 before clearing CR4.PCIDE.
    mov %cr3, %rcx
    and $-4096, %rcx
    mov %rcx, %cr3
.Luefi_pcide_disabled:
    mov %cr4, %rax
    btr $7, %rax
    btr $17, %rax
    btr $20, %rax
    btr $21, %rax
    btr $22, %rax
    mov %rax, %cr4

    # Match the BIOS bootstrap CPU state: enable NXE when supported and WP.
    mov $0x80000000, %eax
    cpuid
    cmp $0x80000001, %eax
    jb .Luefi_nx_done
    mov $0x80000001, %eax
    cpuid
    bt $20, %edx
    jnc .Luefi_nx_done
    mov $0xC0000080, %ecx
    rdmsr
    or $(1 << 11), %eax
    wrmsr
.Luefi_nx_done:
    mov %cr0, %rax
    or $(1 << 16), %rax
    mov %rax, %cr0

    # Clear BSS while firmware mappings and the firmware-provided stack are
    # still active. The emergency IDT storage lives in BSS and is populated
    # only after this clear.
    lea __bss_start(%rip), %rdi
    lea __bss_end(%rip), %rcx
    sub %rdi, %rcx
    xor %eax, %eax
    rep stosb

    # Move onto the kernel-owned stack before replacing the firmware CR3.
    lea stack_top64(%rip), %rsp
    and $-16, %rsp
    xor %rbp, %rbp

    # Install an emergency IDT before switching page tables. Every vector
    # enters a non-returning halt handler, so no firmware IDT memory is needed
    # after the new CR3 becomes active.
    leaq uefi_early_idt(%rip), %rdi
    leaq uefi_early_exception_halt(%rip), %r8
    movl $256, %ecx
.Luefi_early_idt_fill:
    movw %r8w, 0(%rdi)
    movw $0x08, 2(%rdi)
    movb $0, 4(%rdi)
    movb $0x8e, 5(%rdi)
    movq %r8, %rax
    shrq $16, %rax
    movw %ax, 6(%rdi)
    shrq $16, %rax
    movl %eax, 8(%rdi)
    movl $0, 12(%rdi)
    addq $16, %rdi
    loop .Luefi_early_idt_fill
    lidt uefi_early_idtr(%rip)

    # QBoot Protocol v1.1 carries the root of the kernel's 1-GiB identity map.
    testq %r12, %r12
    jz 1f
    movq QM_BOOT_INFO_PAGE_TABLE_ROOT_OFFSET(%r12), %rax
    testq %rax, %rax
    jz 1f
    testq $0xfff, %rax
    jnz 1f
    cmpq $0x3ffbc000, %rax
    ja 1f
    movq %rax, %cr3
    jmp 2f
    1:
    cli
    3:
    hlt
    jmp 3b
    2:

    xor %edi, %edi
    movw %cs, %di
    call idt_init_with_selector

#ifndef QBOOT_BIOS_ONLY
    mov %r12, %rdi
    call main_uefi
#endif

1:
    cli
    hlt
    jmp 1b
2:
    cli
    hlt
    jmp 2b

.size _start64_uefi, .-_start64_uefi

.type uefi_early_exception_halt, @function
uefi_early_exception_halt:
    cli
.Luefi_early_exception_stop:
    hlt
    jmp .Luefi_early_exception_stop
.size uefi_early_exception_halt, .-uefi_early_exception_halt

.section .rodata
.align 8
uefi_early_idtr:
    .word 4095
    .quad uefi_early_idt
.align 8
uefi_gdt:
    .quad 0
    .quad 0x00AF9A000000FFFF  # 0x08: kernel 64-bit code
    .quad 0x00CF92000000FFFF  # 0x10: kernel data
uefi_gdt_end:
uefi_gdt_ptr:
    .word uefi_gdt_end - uefi_gdt - 1
    .quad uefi_gdt

.section .bss
.align 16
uefi_early_idt:
    .skip 4096
