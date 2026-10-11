#include "task.h"
#include "serial.h"

extern void switch_to(struct context **old_ctx, struct context *new_ctx);
extern void task_trampoline(void);
static struct task tasks[TASK_COUNT];
struct task *current_task = 0;
static unsigned int current_index = 0;
static void task_a(void) { uint64_t n = 0; for (;;) { if (++n % 100000U == 0) serial_puts("[task A] running\n"); task_yield(); } }
static void task_b(void) { uint64_t n = 0; for (;;) { if (++n % 100000U == 0) serial_puts("[task B] running\n"); task_yield(); } }
static void (*const entries[TASK_COUNT])(void) = { task_a, task_b };

static void task_init_one(struct task *t, unsigned int id, void (*entry)(void)) {
    uintptr_t top = (uintptr_t)(t->stack + sizeof(t->stack));
    top &= ~(uintptr_t)0xF;
    uint64_t *sp = (uint64_t *)top;
    *(--sp) = (uint64_t)task_exit;      /* return address if entry unexpectedly returns */
    *(--sp) = (uint64_t)task_trampoline;
    *(--sp) = 0; /* rbp */
    *(--sp) = 0; /* rbx */
    *(--sp) = (uint64_t)entry; /* r12 carries entry to trampoline */
    *(--sp) = 0; /* r13 */
    *(--sp) = 0; /* r14 */
    *(--sp) = 0; /* r15 */
    t->ctx = (struct context *)sp; t->id = id; t->switches = 0;
}
void task_init(void) {
    task_init_one(&tasks[0], 0, entries[0]);
    task_init_one(&tasks[1], 1, entries[1]);
    current_index = 0; current_task = &tasks[0];
    serial_puts("Cooperative scheduler initialized (2 tasks).\n");
}
void task_yield(void) {
    unsigned int next_index = (current_index + 1U) % TASK_COUNT;
    struct task *old = current_task;
    struct task *next = &tasks[next_index];
    current_index = next_index; current_task = next;
    old->switches++;
    switch_to(&old->ctx, next->ctx);
}
void task_start(void) __attribute__((noreturn));
void task_start(void) {
    /* Enter first task using the same context-switch path as later yields. */
    struct context *bootstrap_ctx = 0;
    switch_to(&bootstrap_ctx, current_task->ctx);
    for (;;) __asm__ volatile("cli; hlt");
}
void task_exit(void) {
    serial_puts("Task returned unexpectedly; halting.\n");
    for (;;) __asm__ volatile("cli; hlt");
}
