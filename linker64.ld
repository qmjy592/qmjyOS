ENTRY(_start64)

SECTIONS
{
    . = 2M;
    __kernel64_start = .;

    .text :
    {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }

    .rodata :
    {
        *(.rodata .rodata.*)
    }

    .data :
    {
        *(.data .data.*)
    }

    .bss :
    {
        . = ALIGN(16);
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }

    __kernel64_end = .;

    /DISCARD/ :
    {
        *(.eh_frame)
        *(.comment)
    }
}
