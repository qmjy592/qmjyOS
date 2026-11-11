.include "build/qboot/kernel_layout.inc"

.section .text
.code16

.global _start

_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7c00, %sp

    movb 0x0500, %dl
    movb %dl, boot_drive
    sti

    movw $msg_stage2, %si
    call puts16

    call collect_e820
    jc fatal_e820

    movw $msg_e820, %si
    call puts16

    call load_kernel
    jc fatal_disk

    movw $msg_loaded, %si
    call puts16

    cli

    # Enable A20 through the fast A20 gate.
    inb $0x92, %al
    andb $0xfe, %al
    orb $0x02, %al
    outb %al, $0x92

    lgdt gdt_ptr

    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0

    ljmpl $0x08, $protected_entry

puts16:
    movw $0x00e9, %dx
1:
    lodsb
    testb %al, %al
    jz 2f
    outb %al, %dx
    jmp 1b
2:
    ret

# --------------------------------------------------
# BIOS E820 memory map.
# Multiboot-compatible mmap entries are stored at 0x6000.
# --------------------------------------------------

collect_e820:
    movw $0x6000, e820_ptr
    movw $0, e820_count
    movl $0, e820_next

1:
    cmpw $64, e820_count
    jae 5f

    movw e820_ptr, %di
    movl $20, (%di)
    addw $4, %di

    xorw %ax, %ax
    movw %ax, %es

    movl $0xE820, %eax
    movl $0x534D4150, %edx
    movl $20, %ecx
    movl e820_next, %ebx

    int $0x15
    jc 5f

    cmpl $0x534D4150, %eax
    jne 5f

    movl %ebx, e820_next

    addw $24, e820_ptr
    incw e820_count

    testl %ebx, %ebx
    jnz 1b

    cmpw $0, e820_count
    je 5f

    # Multiboot information structure at physical 0x5000.
    # flags bit 6 indicates a valid memory map.
    movl $0x40, 0x5000

    movzwl e820_count, %eax
    imull $24, %eax, %eax
    movl %eax, 0x502c

    movl $0x6000, 0x5030

    clc
    ret

5:
    stc
    ret

# --------------------------------------------------
# Load kernel64.bin from disk.
# Destination staging buffer: 0x10000.
# Maximum transfer per BIOS call: 64 sectors.
# --------------------------------------------------

load_kernel:
    movw $KERNEL_SECTORS, sectors_left
    movl $KERNEL_LBA, current_lba
    movw $0x1000, dest_segment

1:
    cmpw $0, sectors_left
    je 3f

    movw sectors_left, %ax
    cmpw $64, %ax
    jbe 2f

    movw $64, %ax

2:
    movw %ax, dap_count

    movw dest_segment, %ax
    movw %ax, dap_segment

    movl current_lba, %eax
    movl %eax, dap_lba
    movl $0, dap_lba+4

    movw $dap, %si
    movb boot_drive, %dl
    movb $0x42, %ah
    int $0x13
    jc 4f

    movw dap_count, %ax
    subw %ax, sectors_left

    movzwl %ax, %eax
    addl %eax, current_lba

    shlw $5, %ax
    addw %ax, dest_segment

    jmp 1b

3:
    clc
    ret

4:
    stc
    ret

fatal_e820:
    movw $msg_e820_fail, %si
    call puts16
    jmp halt16

fatal_disk:
    movw $msg_disk_fail, %si
    call puts16

halt16:
    cli
1:
    hlt
    jmp 1b

# --------------------------------------------------
# 32-bit protected mode.
# --------------------------------------------------

.code32

protected_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    movl $0x9f000, %esp
    cld

    # Copy sector-padded kernel image to 0x200000.
    movl $0x10000, %esi
    movl $0x200000, %edi
    movl $(KERNEL_SECTORS * 128), %ecx
    rep movsl

    # Clear three page-table pages at 0x70000.
    movl $0x70000, %edi
    xorl %eax, %eax
    movl $3072, %ecx
    rep stosl

    # PML4 -> PDPT -> PD.
    movl $0x71003, 0x70000
    movl $0x72003, 0x71000

    # Identity-map the first 1 GiB with 2 MiB pages.
    movl $0x72000, %edi
    movl $0x83, %eax
    movl $512, %ecx

1:
    movl %eax, (%edi)
    movl $0, 4(%edi)

    addl $0x200000, %eax
    addl $8, %edi
    loop 1b

    movl $0x70000, %eax
    movl %eax, %cr3

    # Enable PAE.
    movl %cr4, %eax
    orl $(1 << 5), %eax
    movl %eax, %cr4

    # Check long-mode support.
    movl $0x80000000, %eax
    cpuid
    cmpl $0x80000001, %eax
    jb no_long_mode

    movl $0x80000001, %eax
    cpuid
    btl $29, %edx
    jnc no_long_mode

    # Enable EFER.LME and enable NXE when CPU supports NX.
    # CPUID.80000001H:EDX[20] = NX support.
    btl $20, %edx
    setc %bl

    movl $0xC0000080, %ecx
    rdmsr

    # Enable Long Mode.
    orl $(1 << 8), %eax

    # Enable NX only when supported by the CPU.
    testb %bl, %bl
    jz 1f
    orl $(1 << 11), %eax

1:
    wrmsr

    # Enable paging and write protection.
    movl %cr0, %eax
    orl $0x80010000, %eax
    movl %eax, %cr0

    ljmp $0x18, $long_mode_entry

no_long_mode:
    movw $0x00e9, %dx
    movl $msg_no_lm, %esi

1:
    lodsb
    testb %al, %al
    jz 2f
    outb %al, %dx
    jmp 1b

2:
    cli
3:
    hlt
    jmp 3b

# --------------------------------------------------
# 64-bit kernel handoff.
# --------------------------------------------------

.code64

long_mode_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss

    movq $0x9f000, %rsp

    # Print diagnostic BEFORE setting Multiboot registers.
    movw $0x00e9, %dx
    movq $msg_handoff, %rsi

1:
    lodsb
    testb %al, %al
    jz 2f
    outb %al, %dx
    jmp 1b

2:
    # Multiboot v1 bootloader magic.
    movl $0x2BADB002, %eax

    # Multiboot information structure.
    movl $0x5000, %ebx

    # Jump without destroying EAX or EBX.
    movl $0x200000, %edi
    jmp *%rdi

.align 8

gdt:
    .quad 0
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
    .quad 0x00AF9A000000FFFF

gdt_end:

gdt_ptr:
    .word gdt_end - gdt - 1
    .long gdt

.align 4

dap:
    .byte 0x10, 0
dap_count:
    .word 0
    .word 0
dap_segment:
    .word 0
dap_lba:
    .quad 0

boot_drive:
    .byte 0

sectors_left:
    .word 0

dest_segment:
    .word 0

current_lba:
    .long 0

e820_ptr:
    .word 0

e820_count:
    .word 0

e820_next:
    .long 0

msg_stage2:
    .asciz "QBOOT_STAGE2_OK\n"

msg_e820:
    .asciz "QBOOT_E820_OK\n"

msg_loaded:
    .asciz "QBOOT_KERNEL_LOADED\n"

msg_e820_fail:
    .asciz "QBOOT_E820_FAIL\n"

msg_disk_fail:
    .asciz "QBOOT_DISK_READ_FAIL\n"

msg_no_lm:
    .asciz "QBOOT_NO_LONG_MODE\n"

msg_handoff:
    .asciz "QBOOT_LONG_MODE_HANDOFF\n"

.org 8191
.byte 0
