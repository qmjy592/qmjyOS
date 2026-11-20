#ifndef TASK_H
#define TASK_H
#include <stdint.h>
#include "paging.h"
#define TASK_COUNT 2
#define TASK_STACK_SIZE 4096
struct context { uint64_t r15, r14, r13, r12, rbx, rbp; };
struct task {
    struct context *ctx;
    uint8_t stack[TASK_STACK_SIZE] __attribute__((aligned(16)));
    uint32_t id;
    uint64_t switches;
    vm_address_space_t *address_space;
};
void task_init(void);
int task_set_address_space(uint32_t task_id, vm_address_space_t *address_space);
void task_start(void) __attribute__((noreturn));
void task_yield(void);
void task_exit(void) __attribute__((noreturn));
extern struct task *current_task;
#endif
