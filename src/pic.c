 #include "pic.h"
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

static inline void outb(unsigned short port, unsigned char val) {
    __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port));
}

void pic_remap(void) {
    outb(PIC1_CMD, 0x11); outb(PIC2_CMD, 0x11);
    outb(PIC1_DATA, 0x20); outb(PIC2_DATA, 0x28);
    outb(PIC1_DATA, 0x04); outb(PIC2_DATA, 0x02);
    outb(PIC1_DATA, 0x01); outb(PIC2_DATA, 0x01);
    // 仅开放主 PIC 的 IRQ0（定时器）和 IRQ1（键盘）。
    // 从 PIC 的 IRQ8-15 全部屏蔽，直到为它们安装处理入口。
    outb(PIC1_DATA, 0xFC);
    outb(PIC2_DATA, 0xFF);
}

void pic_send_eoi(unsigned char irq) {
    if (irq >= 8) outb(PIC2_CMD, 0x20);
    outb(PIC1_CMD, 0x20);
}
