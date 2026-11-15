#include "qmjy/kernel.h"
#include <stddef.h>

#define QM_INIT_MAX_ENTRIES 64U
#define QM_INIT_NAME_MAX 64U
#define QM_INIT_LEVEL_COUNT 7U

enum init_entry_state { INIT_ENTRY_READY = 0, INIT_ENTRY_RUNNING, INIT_ENTRY_DONE, INIT_ENTRY_FAILED };
enum init_level_state { INIT_LEVEL_NEW = 0, INIT_LEVEL_RUNNING, INIT_LEVEL_DONE, INIT_LEVEL_FAILED };
enum kernel_state { KERNEL_NEW = 0, KERNEL_INITIALIZING, KERNEL_READY, KERNEL_FAILED, KERNEL_STOPPED };

struct init_record {
    char name[QM_INIT_NAME_MAX];
    qm_kernel_init_level_t level;
    qm_kernel_init_fn_t initialize;
    enum init_entry_state state;
    qm_status_t error;
};

static struct init_record g_entries[QM_INIT_MAX_ENTRIES];
static size_t g_entry_count;
static enum init_level_state g_level_state[QM_INIT_LEVEL_COUNT];
static qm_status_t g_level_error[QM_INIT_LEVEL_COUNT];
static enum kernel_state g_kernel_state;

static size_t string_length(const char *s)
{
    size_t n = 0;
    if (s == NULL) return 0;
    while (s[n] != '\0') ++n;
    return n;
}

static int string_equal(const char *a, const char *b)
{
    size_t i = 0;
    if (a == NULL || b == NULL) return 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) return 0;
        ++i;
    }
    return a[i] == b[i];
}

static int any_level_started(void)
{
    size_t i;
    for (i = 0; i < QM_INIT_LEVEL_COUNT; ++i)
        if (g_level_state[i] != INIT_LEVEL_NEW) return 1;
    return 0;
}

qm_status_t qm_kernel_init_register(const qm_kernel_init_entry_t *entry)
{
    size_t i;
    size_t n;
    struct init_record *record;

    if (entry == NULL || entry->name == NULL || entry->initialize == NULL ||
        entry->level < QM_KERNEL_INIT_EARLY || entry->level > QM_KERNEL_INIT_LATE)
        return QM_ERR_INVALID;

    n = string_length(entry->name);
    if (n == 0 || n >= QM_INIT_NAME_MAX)
        return QM_ERR_INVALID;

    if (g_kernel_state != KERNEL_NEW || any_level_started())
        return QM_ERR_STATE;

    for (i = 0; i < g_entry_count; ++i)
        if (string_equal(g_entries[i].name, entry->name))
            return QM_ERR_EXISTS;

    if (g_entry_count >= QM_INIT_MAX_ENTRIES)
        return QM_ERR_NO_SPACE;

    record = &g_entries[g_entry_count++];
    for (i = 0; i <= n; ++i)
        record->name[i] = entry->name[i];
    record->level = entry->level;
    record->initialize = entry->initialize;
    record->state = INIT_ENTRY_READY;
    record->error = QM_OK;
    return QM_OK;
}

qm_status_t qm_kernel_init_run_level(qm_kernel_init_level_t level)
{
    size_t i;
    qm_status_t status;

    unsigned int earlier;

    if (level < QM_KERNEL_INIT_EARLY || level > QM_KERNEL_INIT_LATE)
        return QM_ERR_INVALID;
    if (g_kernel_state == KERNEL_FAILED || g_kernel_state == KERNEL_STOPPED)
        return QM_ERR_STATE;

    for (earlier = 0; earlier < (unsigned int)level; ++earlier)
        if (g_level_state[earlier] != INIT_LEVEL_DONE)
            return QM_ERR_DEPENDENCY;

    if (g_level_state[level] == INIT_LEVEL_DONE)
        return QM_OK;
    if (g_level_state[level] == INIT_LEVEL_RUNNING)
        return QM_ERR_BUSY;
    if (g_level_state[level] == INIT_LEVEL_FAILED)
        return g_level_error[level];

    /* Init callbacks run before SMP/preemption is enabled; no lock is needed here. */
    g_level_state[level] = INIT_LEVEL_RUNNING;
    for (i = 0; i < g_entry_count; ++i) {
        struct init_record *record = &g_entries[i];
        if (record->level != level || record->state == INIT_ENTRY_DONE)
            continue;
        if (record->state == INIT_ENTRY_FAILED) {
            g_level_state[level] = INIT_LEVEL_FAILED;
            g_level_error[level] = record->error;
            return record->error;
        }
        record->state = INIT_ENTRY_RUNNING;
        status = record->initialize();
        if (status != QM_OK) {
            record->state = INIT_ENTRY_FAILED;
            record->error = status;
            g_level_state[level] = INIT_LEVEL_FAILED;
            g_level_error[level] = status;
            return status;
        }
        record->state = INIT_ENTRY_DONE;
    }

    g_level_state[level] = INIT_LEVEL_DONE;
    g_level_error[level] = QM_OK;
    return QM_OK;
}

qm_status_t qm_kernel_init(const qm_boot_info_t *boot_info)
{
    unsigned int level;
    qm_status_t status;

    if (g_kernel_state != KERNEL_NEW)
        return QM_ERR_STATE;

    status = qm_boot_validate_info(boot_info);
    if (status != QM_OK) {
        g_kernel_state = KERNEL_FAILED;
        return status;
    }

    g_kernel_state = KERNEL_INITIALIZING;
    for (level = QM_KERNEL_INIT_EARLY; level <= QM_KERNEL_INIT_LATE; ++level) {
        status = qm_kernel_init_run_level((qm_kernel_init_level_t)level);
        if (status != QM_OK) {
            g_kernel_state = KERNEL_FAILED;
            return status;
        }
    }

    g_kernel_state = KERNEL_READY;
    return QM_OK;
}

void qm_kernel_shutdown(int reason)
{
    (void)reason;
    if (g_kernel_state == KERNEL_READY || g_kernel_state == KERNEL_FAILED)
        g_kernel_state = KERNEL_STOPPED;
}
