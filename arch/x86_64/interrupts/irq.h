#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>

/* Called by irq_entry.S with the remapped IDT vector (32 or 33). */
void irq_handler(uint64_t vector);

#endif
