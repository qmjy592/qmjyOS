#include "serial.h"
#include <stdint.h>

const char *exception_messages[] = {
    "Division Error", "Debug", "Non-maskable Interrupt", "Breakpoint",
    "Overflow", "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
    "Stack-Segment Fault", "General Protection Fault", "Page Fault", "Reserved",
    "x87 Floating-Point Exception", "Alignment Check", "Machine Check", "SIMD Floating-Point Exception",
    "Virtualization Exception", "Control Protection Exception", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection Exception", "VMM Communication Exception", "Security Exception", "Reserved"
};

void print_hex(uint64_t val) {
    char buf[19];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        uint8_t nibble = (val >> (60 - i * 4)) & 0xF;
        buf[2 + i] = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
    }
    buf[18] = '\0';
    serial_puts(buf);
}

void exception_handler(uint64_t int_no, uint64_t err_code, uint64_t rip) {
    serial_puts("\n\n!!! KERNEL PANIC !!!\n");
    serial_puts("Exception: ");
    if (int_no < 32) {
        serial_puts(exception_messages[int_no]);
    } else {
        serial_puts("Unknown");
    }
    serial_puts("\nError Code: ");
    print_hex(err_code);
    serial_puts("\nRIP: ");
    print_hex(rip);
    if (int_no == 14) {
        uint64_t cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        serial_puts("\nCR2: ");
        print_hex(cr2);
    }
    serial_puts("\n\nSystem Halted.\n");
    while (1) {
        __asm__ volatile("cli; hlt");
    }
}
