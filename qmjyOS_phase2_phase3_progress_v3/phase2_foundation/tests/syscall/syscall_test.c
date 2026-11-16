#include "qmjy/syscall.h"
#include <assert.h>
#include <stdio.h>

static qm_syscall_result_t add_handler(const qm_syscall_context_t *ctx)
{
    return (qm_syscall_result_t)(ctx->arguments[0] + ctx->arguments[1]);
}

int main(void)
{
    qm_syscall_context_t ctx = {0};
    assert(qm_syscall_register(3, add_handler) == QM_OK);
    assert(qm_syscall_register(3, add_handler) == QM_ERR_EXISTS);
    assert(qm_syscall_register(4, NULL) == QM_ERR_INVALID);
    assert(qm_syscall_register(128, add_handler) == QM_ERR_RANGE);

    ctx.number = 3;
    ctx.arguments[0] = 19;
    ctx.arguments[1] = 23;
    assert(qm_syscall_dispatch(&ctx) == 42);

    ctx.number = 4;
    assert(qm_syscall_dispatch(&ctx) == QM_ERR_NOT_SUPPORTED);
    ctx.number = 128;
    assert(qm_syscall_dispatch(&ctx) == QM_ERR_RANGE);
    assert(qm_syscall_dispatch(NULL) == QM_ERR_INVALID);

    puts("System-call registry and dispatch tests: PASS");
    return 0;
}
