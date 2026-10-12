ENTRY(_start)
SECTIONS
{
    . = 1M;
    __kernel_start = .;
    .text : {
    KEEP(*(.multiboot))
    *(.text .text.*)
}

    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.*) *(COMMON) }
    __kernel_end = .;
    /DISCARD/ : { *(.eh_frame) *(.comment) }
}
