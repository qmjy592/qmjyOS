#include "irq.h"
#include "pic.h"
#include "timer.h"
#include <stdint.h>
void irq_handler(uint64_t irq_no) {
    if (irq_no == 32) { timer_tick(); pic_send_eoi(0); }
    else if (irq_no == 33) { pic_send_eoi(1); }
}
