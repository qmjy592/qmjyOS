#ifndef QMJY_PANIC_H
#define QMJY_PANIC_H

#include "types.h"
#include "compiler.h"

typedef struct qm_panic_context {
    const char *file;
    const char *function;
    qm_u32_t line;
    qm_u32_t cpu_id;
    qm_u64_t error_code;
} qm_panic_context_t;

QMjy_NORETURN void qm_panic(
    const char *message,
    const qm_panic_context_t *context
);

#define QM_PANIC(message) \
    do { \
        const qm_panic_context_t qm_ctx = { \
            __FILE__, __func__, \
            (qm_u32_t)__LINE__, 0U, 0U \
        }; \
        qm_panic((message), &qm_ctx); \
    } while (0)

#endif
