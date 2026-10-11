ENTRY(_start)
SECTIONS
{
    . = 1M;
    .text : {
    KEEP(*(.multiboot))
    *(.text .text.*)
}

    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.*) *(COMMON) }
    /DISCARD/ : { *(.eh_frame) *(.comment) }
}
