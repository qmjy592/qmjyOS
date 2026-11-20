#include "task.h"
#include "paging.h"

#include <assert.h>
#include <stdio.h>

void serial_puts(const char *text)
{
    (void)text;
}

void task_trampoline(void)
{
}

void switch_to(struct context **old_context, struct context *new_context)
{
    if (old_context != NULL)
        *old_context = new_context;
}

int main(void)
{
    vm_address_space_t first = {0};
    vm_address_space_t second = {0};

    assert(vm_init() == VM_OK);
    assert(vm_address_space_create(&first) == VM_OK);
    assert(vm_address_space_create(&second) == VM_OK);

    task_init();
    assert(task_set_address_space(0, &first) == 0);
    assert(task_set_address_space(1, &second) == 0);
    assert(task_set_address_space(TASK_COUNT, &first) != 0);

    /* task_yield must activate the next task's root before context switching. */
    task_yield();
    assert(current_task != NULL && current_task->id == 1U);
    assert(vm_current_root_physical() == second.root_physical);

    task_yield();
    assert(current_task != NULL && current_task->id == 0U);
    assert(vm_current_root_physical() == first.root_physical);

    /* A task without an explicit user root returns to the kernel address space. */
    assert(task_set_address_space(1, NULL) == 0);
    task_yield();
    assert(current_task != NULL && current_task->id == 1U);
    assert(vm_current_root_physical() == vm_root_physical());

    assert(vm_address_space_destroy(&first) == VM_OK);
    assert(vm_address_space_destroy(&second) == VM_OK);
    assert(vm_tables_used() == 3U);

    puts("Scheduler per-task CR3 activation tests: PASS");
    return 0;
}
