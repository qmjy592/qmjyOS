#ifndef QMJY_LOG_H
#define QMJY_LOG_H

#include "types.h"
#include "error.h"

typedef enum qm_log_level {
    QM_LOG_TRACE = 0,
    QM_LOG_DEBUG = 1,
    QM_LOG_INFO = 2,
    QM_LOG_WARN = 3,
    QM_LOG_ERROR = 4,
    QM_LOG_FATAL = 5
} qm_log_level_t;

typedef void (*qm_log_sink_fn_t)(
    qm_log_level_t level,
    const char *message,
    qm_size_t length,
    void *context
);

qm_status_t qm_log_register_sink(
    qm_log_sink_fn_t sink,
    void *context
);

void qm_log_write(
    qm_log_level_t level,
    const char *message,
    qm_size_t length
);

void qm_log_set_level(qm_log_level_t minimum_level);

#endif
