ENTRY(_start)

SECTIONS
{
    . = 1M;
    __kernel_start = .;

    .multiboot :
    {
        KEEP(*(.multiboot))
    }

    .text :
    {
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
        *(.bss .bss.*)
        *(COMMON)
    }

    __kernel_end = .;

    ASSERT(SIZEOF(.multiboot) >= 12,
           "Multiboot v1 header missing or truncated")

    ASSERT(ADDR(.multiboot) == 0x100000,
           "Multiboot header must begin at 1 MiB")

    ASSERT(_start == 0x10000C,
           "Unexpected 32-bit entry address")

    /DISCARD/ :
    {
        *(.eh_frame)
        *(.comment)
    }
}
