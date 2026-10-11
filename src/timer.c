#include "timer.h"
#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_FREQ     1193182U
static volatile uint64_t ticks = 0;
static inline void outb(unsigned short port, unsigned char val) { __asm__ volatile("outb %0, %1" :: "a"(val), "Nd"(port)); }
void timer_init(uint32_t frequency) {
    if (frequency == 0) frequency = 100;
    uint32_t divisor = PIT_FREQ / frequency;
    if (divisor == 0) divisor = 1;
    if (divisor > 65535U) divisor = 65535U;
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));
}
void timer_tick(void) { ticks++; }
uint64_t timer_get_ticks(void) { return ticks; }
