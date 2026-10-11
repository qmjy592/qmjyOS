#include "serial.h"
#define COM1 0x3F8
static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}
static inline unsigned char inb(unsigned short port) {
    unsigned char r;
    __asm__ volatile("inb %1, %0" : "=a"(r) : "Nd"(port));
    return r;
}
void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}
static int tx_empty(void) { return inb(COM1 + 5) & 0x20; }
void serial_putc(char c) {
    while (!tx_empty());
    outb(COM1, (unsigned char)c);
}
void serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}
