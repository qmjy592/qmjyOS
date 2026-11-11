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

    movb %dl, boot_drive
    movb %dl, 0x0500

    # Check BIOS Extended Disk Drive support.
    movw $0x55aa, %bx
    movb $0x41, %ah
    int $0x13
    jc disk_error

    cmpw $0xaa55, %bx
    jne disk_error

    testw $1, %cx
    jz disk_error

    # Load 16 sectors of Stage 2 to 0x8000.
    sti
    movw $dap, %si
    movb boot_drive, %dl
    movb $0x42, %ah
    int $0x13
    jc disk_error

    ljmp $0x0000, $0x8000

disk_error:
    movw $disk_error_message, %si
    movw $0x00e9, %dx

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

.align 4

dap:
    .byte 0x10
    .byte 0
    .word 16
    .word 0x8000
    .word 0x0000
    .quad 1

boot_drive:
    .byte 0

disk_error_message:
    .asciz "QBOOT_STAGE1_DISK_FAIL"

.org 510
.word 0xaa55
