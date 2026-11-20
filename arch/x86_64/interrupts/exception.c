#include "serial.h"
#include "qmjy/process_runtime.h"
#include "qmjy/x86_64_trap.h"
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

qm_u64_t exception_handler(const qm_x86_64_trap_frame_t *frame)
{
    uint64_t cr2 = 0;
    if (frame == NULL)
        goto kernel_panic;

    /* A CPL3 exception belongs to the current user process, not the kernel. */
    if ((frame->cs & 3U) == 3U) {
        qm_s32_t exit_code = (qm_s32_t)(-128 - (qm_s32_t)frame->vector);
        if (qm_process_runtime_exit_current(exit_code) == QM_OK) {
            panic_puts("\n[Ring 3] User process faulted; isolated process terminated.\n");
            return QM_X86_64_TRAP_EXIT_TO_KERNEL;
        }
        panic_puts("\n[Ring 3] User exception could not be contained.\n");
    }

kernel_panic:
    panic_puts("\n\n!!! KERNEL PANIC !!!\nException: ");
    if (frame != NULL && frame->vector < 32)
        panic_puts(exception_messages[frame->vector]);
    else
        panic_puts("Unknown");
    if (frame != NULL) {
        panic_puts("\nVector: ");     panic_hex(frame->vector);
        panic_puts("\nError Code: "); panic_hex(frame->error_code);
        panic_puts("\nRIP: ");        panic_hex(frame->rip);
        panic_puts("\nCS: ");         panic_hex(frame->cs);
        panic_puts("\nRFLAGS: ");     panic_hex(frame->rflags);
    }
    if (frame != NULL && frame->vector == 14) {
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        panic_puts("\nCR2: ");
        panic_hex(cr2);
    }
    panic_puts("\n\nSystem Halted.\n");
    for (;;)
        __asm__ volatile("cli; hlt" ::: "memory");
}
