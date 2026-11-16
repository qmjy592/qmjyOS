#include "serial.h"
#include <stdint.h>

static const char *exception_messages[] = {
    "Division Error", "Debug", "Non-maskable Interrupt", "Breakpoint",
    "Overflow", "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
    "Stack-Segment Fault", "General Protection Fault", "Page Fault", "Reserved",
    "x87 Floating-Point Exception", "Alignment Check", "Machine Check", "SIMD Floating-Point Exception",
    "Virtualization Exception", "Control Protection Exception", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection Exception", "VMM Communication Exception", "Security Exception", "Reserved"
};

static void panic_putc(char value)
{
    __asm__ volatile("outb %0, %1" :: "a"((uint8_t)value), "Nd"((uint16_t)0xE9));
    if (serial_is_initialized())
        serial_putc(value);
}

static void panic_puts(const char *text)
{
    while (*text) {
        if (*text == '\n')
            panic_putc('\r');
        panic_putc(*text++);
    }
}

static void panic_hex(uint64_t value)
{
    static const char digits[] = "0123456789ABCDEF";
    unsigned int i;
    panic_putc('0');
    panic_putc('x');
    for (i = 0; i < 16; ++i)
        panic_putc(digits[(value >> (60U - i * 4U)) & 0x0FU]);
}

void exception_handler(uint64_t int_no, uint64_t err_code,
                       uint64_t rip, uint64_t cs, uint64_t rflags)
{
    uint64_t cr2 = 0;
    panic_puts("\n\n!!! KERNEL PANIC !!!\nException: ");
    if (int_no < 32)
        panic_puts(exception_messages[int_no]);
    else
        panic_puts("Unknown");
    panic_puts("\nVector: ");     panic_hex(int_no);
    panic_puts("\nError Code: "); panic_hex(err_code);
    panic_puts("\nRIP: ");        panic_hex(rip);
    panic_puts("\nCS: ");         panic_hex(cs);
    panic_puts("\nRFLAGS: ");     panic_hex(rflags);
    if (int_no == 14) {
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        panic_puts("\nCR2: ");
        panic_hex(cr2);
    }
    panic_puts("\n\nSystem Halted.\n");
    for (;;)
        __asm__ volatile("cli; hlt" ::: "memory");
}
