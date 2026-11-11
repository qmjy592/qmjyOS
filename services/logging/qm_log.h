#ifndef QM_LOG_H
#define QM_LOG_H

#include <stddef.h>
#include <stdint.h>

#define QM_LOG_CAPACITY 128
#define QM_LOG_TAG_MAX  24
#define QM_LOG_TEXT_MAX 128

typedef enum {
    QM_LOG_DEBUG = 0,
    QM_LOG_INFO,
    QM_LOG_WARN,
    QM_LOG_ERROR
} qm_log_level_t;

typedef struct {
    uint64_t sequence;
    qm_log_level_t level;
    char tag[QM_LOG_TAG_MAX];
    char message[QM_LOG_TEXT_MAX];
} qm_log_record_t;

void qm_log_init(void);

int qm_log_write(
    qm_log_level_t level,
    const char *tag,
    const char *message
);

int qm_log_read(qm_log_record_t *record);

size_t qm_log_count(void);

uint64_t qm_log_dropped(void);

const char *qm_log_level_name(qm_log_level_t level);

#endif
