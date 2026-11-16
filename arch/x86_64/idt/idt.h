#ifndef IDT_H
#define IDT_H
#include <stdint.h>
void idt_init(void);
void idt_init_with_selector(uint16_t selector);
#endif
