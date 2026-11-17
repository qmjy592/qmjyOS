ENTRY(_start64)

/*
 * The UEFI ELF loader maps PT_LOAD segments with 4 KiB pages and enforces
 * page-granular W^X. Keep each permission class in a distinct, page-aligned
 * PT_LOAD segment. The same layout is also consumed by the BIOS binary path.
 */
PHDRS
{
    text   PT_LOAD FLAGS(5); /* R | X */
    rodata PT_LOAD FLAGS(4); /* R */
    data   PT_LOAD FLAGS(6); /* R | W */
}

SECTIONS
{
    . = 0x00200000;
    __kernel64_start = .;

    .text ALIGN(0x1000) :
    {
        __text_start = .;
        KEEP(*(.text.boot))
        *(.text .text.*)
        . = ALIGN(0x1000);
        __text_end = .;
    } :text

    . = ALIGN(0x1000);
    .rodata :
    {
        __rodata_start = .;
        *(.rodata .rodata.*)
        *(.srodata .srodata.*)
        . = ALIGN(0x1000);
        __rodata_end = .;
    } :rodata

    . = ALIGN(0x1000);
    .data :
    {
        __data_start = .;
        *(.data .data.*)
        *(.sdata .sdata.*)
        *(.got .got.*)
        *(.init_array .init_array.*)
        *(.fini_array .fini_array.*)
        . = ALIGN(0x1000);
        __data_end = .;
    } :data

    . = ALIGN(0x1000);
    .bss (NOLOAD) :
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(0x1000);
        __bss_end = .;
    } :data

    __kernel64_end = .;

    /DISCARD/ :
    {
        *(.eh_frame .eh_frame.*)
        *(.comment)
        *(.note .note.*)
        *(.note.GNU-stack)
    }

    ASSERT((ADDR(.text) & 0xFFF) == 0, "kernel .text is not 4 KiB aligned")
    ASSERT((ADDR(.rodata) & 0xFFF) == 0, "kernel .rodata is not 4 KiB aligned")
    ASSERT((ADDR(.data) & 0xFFF) == 0, "kernel .data is not 4 KiB aligned")
    ASSERT((ADDR(.bss) & 0xFFF) == 0, "kernel .bss is not 4 KiB aligned")
}
