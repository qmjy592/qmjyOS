.set MB_MAGIC,    0x1BADB002
.set MB_FLAGS,    0x0
.set MB_CHECKSUM, -(MB_MAGIC + MB_FLAGS)

.section .multiboot, "a"
.align 4
.long MB_MAGIC
.long MB_FLAGS
.long MB_CHECKSUM

.section .bss
.align 4096
pml4:  .space 4096
pdpt:  .space 4096
pd:    .space 4096
.align 16
stack_bottom:
    .space 16384
.global stack_top
stack_top:

.section .rodata
.align 8
gdt32:
    .quad 0
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
gdt32_ptr:
    .word gdt32_ptr - gdt32 - 1
    .long gdt32

gdt64:
    .quad 0
    .quad 0x00AF9A000000FFFF
    .quad 0x00CF92000000FFFF
gdt64_ptr:
    .word gdt64_ptr - gdt64 - 1
    .long gdt64
    .long 0
.section .text
.code32
.global _start
_start:
    cli
    lgdt gdt32_ptr
    ljmp $0x08, $reload_cs
reload_cs:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov $stack_top, %esp
    mov %esp, %ebp

    # 把嵌入的 64 位内核复制到 2MB 处
    mov $_binary_kernel64_bin_start, %esi
    mov $0x200000, %edi
    mov $_binary_kernel64_bin_end, %ecx
    sub $_binary_kernel64_bin_start, %ecx
    rep movsb

    # 建立页表：映射前 1GB
    mov $pml4, %edi
    mov $pdpt, %eax
    or  $0x3, %eax
    mov %eax, (%edi)
    movl $0, 4(%edi)

    mov $pdpt, %edi
    mov $pd, %eax
    or  $0x3, %eax
    mov %eax, (%edi)
    movl $0, 4(%edi)

    mov $pd, %edi
    mov $0x83, %eax
    mov $512, %ecx
1:
    mov %eax, (%edi)
    movl $0, 4(%edi)
    add $0x200000, %eax
    add $8, %edi
    loop 1b

    # 开启长模式
    mov $pml4, %eax
    mov %eax, %cr3
    mov %cr4, %eax
    or  $(1 << 5), %eax
    mov %eax, %cr4
    mov $0xC0000080, %ecx
    rdmsr
    or  $(1 << 8), %eax
    wrmsr
    mov %cr0, %eax
    or  $(1 << 31), %eax
    mov %eax, %cr0
    lgdt gdt64_ptr
    .byte 0xEA
    .long 0x200000
    .word 0x08
