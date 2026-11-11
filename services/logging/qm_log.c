#include "qm_log.h"

static qm_log_record_t g_records[QM_LOG_CAPACITY];
static size_t g_head;
static size_t g_tail;
static size_t g_count;
static uint64_t g_sequence;
static uint64_t g_dropped;

static size_t qm_log_strlen(const char *s)
{
    size_t n = 0;

    if (!s)
        return 0;

    while (s[n])
        ++n;

    return n;
}

static void qm_log_copy(
    char *destination,
    size_t capacity,
    const char *source
)
{
    size_t i = 0;

    if (!destination || capacity == 0)
        return;

    while (source && source[i] && i + 1 < capacity) {
        destination[i] = source[i];
        ++i;
    }

    destination[i] = '\0';
}

void qm_log_init(void)
{
    size_t i;

    for (i = 0; i < QM_LOG_CAPACITY; ++i) {
        g_records[i].sequence = 0;
        g_records[i].level = QM_LOG_INFO;
        g_records[i].tag[0] = '\0';
        g_records[i].message[0] = '\0';
    }

    g_head = 0;
    g_tail = 0;
    g_count = 0;
    g_sequence = 0;
    g_dropped = 0;
}

int qm_log_write(
    qm_log_level_t level,
    const char *tag,
    const char *message
)
{
    qm_log_record_t *record;

    if (level < QM_LOG_DEBUG || level > QM_LOG_ERROR)
        return -1;

    if (!tag || !tag[0] || !message)
        return -1;

    if (qm_log_strlen(tag) >= QM_LOG_TAG_MAX)
        return -1;

    if (g_count == QM_LOG_CAPACITY) {
        g_head = (g_head + 1) % QM_LOG_CAPACITY;
        --g_count;
        ++g_dropped;
    }

    record = &g_records[g_tail];

    ++g_sequence;
    record->sequence = g_sequence;
    record->level = level;

    qm_log_copy(record->tag, sizeof(record->tag), tag);
    qm_log_copy(
        record->message,
        sizeof(record->message),
        message
    );

    g_tail = (g_tail + 1) % QM_LOG_CAPACITY;
    ++g_count;

    return 0;
}

int qm_log_read(qm_log_record_t *record)
{
    if (!record)
        return -1;

    if (g_count == 0)
        return -2;

    *record = g_records[g_head];

    g_head = (g_head + 1) % QM_LOG_CAPACITY;
    --g_count;

    return 0;
}

size_t qm_log_count(void)
{
    return g_count;
}

uint64_t qm_log_dropped(void)
{
    return g_dropped;
}

const char *qm_log_level_name(qm_log_level_t level)
{
    switch (level) {
    case QM_LOG_DEBUG:
        return "DEBUG";
    case QM_LOG_INFO:
        return "INFO";
    case QM_LOG_WARN:
        return "WARN";
    case QM_LOG_ERROR:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}
