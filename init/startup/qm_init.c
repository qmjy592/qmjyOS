#include "qm_init.h"

#include <string.h>

static qm_init_entry_t g_entries[QM_INIT_MAX_ENTRIES];
static size_t g_count;
static qm_init_stage_t g_next_stage;
static int g_running;

static int qm_init_valid_stage(qm_init_stage_t stage)
{
    return stage >= QM_INIT_STAGE_EARLY &&
           stage < QM_INIT_STAGE_COUNT;
}

static int qm_init_find(const char *name)
{
    size_t i;

    if (name == NULL) {
        return -1;
    }

    for (i = 0; i < g_count; ++i) {
        if (strcmp(g_entries[i].name, name) == 0) {
            return (int)i;
        }
    }

    return -1;
}

void qm_init_init(void)
{
    memset(g_entries, 0, sizeof(g_entries));
    g_count = 0;
    g_next_stage = QM_INIT_STAGE_EARLY;
    g_running = 0;
}

int qm_init_register(
    const char *name,
    qm_init_stage_t stage,
    int priority,
    qm_init_callback_t callback
)
{
    size_t length;
    qm_init_entry_t *entry;

    if (name == NULL || callback == NULL ||
        !qm_init_valid_stage(stage)) {
        return QM_INIT_ERR_INVALID;
    }

    if (g_running) {
        return QM_INIT_ERR_STATE;
    }

    length = strlen(name);

    if (length == 0 || length >= QM_INIT_NAME_MAX) {
        return QM_INIT_ERR_INVALID;
    }

    if (stage < g_next_stage) {
        return QM_INIT_ERR_STAGE_ORDER;
    }

    if (qm_init_find(name) >= 0) {
        return QM_INIT_ERR_DUPLICATE;
    }

    if (g_count >= QM_INIT_MAX_ENTRIES) {
        return QM_INIT_ERR_FULL;
    }

    entry = &g_entries[g_count];

    memcpy(entry->name, name, length + 1);
    entry->callback = callback;
    entry->stage = stage;
    entry->priority = priority;
    entry->state = QM_INIT_STATE_PENDING;
    entry->callback_result = 0;

    ++g_count;
    return QM_INIT_OK;
}

int qm_init_run_stage(qm_init_stage_t stage)
{
    size_t completed = 0;
    size_t stage_entries = 0;
    size_t i;

    int last_index = -1;
    int selected;
    int callback_result;

    if (!qm_init_valid_stage(stage)) {
        return QM_INIT_ERR_INVALID;
    }

    if (g_running) {
        return QM_INIT_ERR_STATE;
    }

    if (stage != g_next_stage) {
        return QM_INIT_ERR_STAGE_ORDER;
    }

    for (i = 0; i < g_count; ++i) {
        if (g_entries[i].stage == stage) {
            if (g_entries[i].state != QM_INIT_STATE_PENDING) {
                return QM_INIT_ERR_STATE;
            }

            ++stage_entries;
        }
    }

    g_running = 1;

    while (completed < stage_entries) {
        selected = -1;

        for (i = 0; i < g_count; ++i) {
            qm_init_entry_t *candidate = &g_entries[i];

            if (candidate->stage != stage ||
                candidate->state != QM_INIT_STATE_PENDING) {
                continue;
            }

            if (last_index >= 0 && (int)i <= last_index) {
                continue;
            }

            if (selected < 0 ||
                candidate->priority <
                    g_entries[selected].priority) {
                selected = (int)i;
            }
        }

        /*
         * If a lower-priority entry was registered after a
         * higher-priority entry, restart selection from the
         * beginning while excluding already completed entries.
         */
        if (selected < 0) {
            last_index = -1;

            for (i = 0; i < g_count; ++i) {
                qm_init_entry_t *candidate = &g_entries[i];

                if (candidate->stage != stage ||
                    candidate->state != QM_INIT_STATE_PENDING) {
                    continue;
                }

                if (selected < 0 ||
                    candidate->priority <
                        g_entries[selected].priority) {
                    selected = (int)i;
                }
            }
        }

        if (selected < 0) {
            g_running = 0;
            return QM_INIT_ERR_STATE;
        }

        g_entries[selected].state = QM_INIT_STATE_RUNNING;
        callback_result = g_entries[selected].callback();
        g_entries[selected].callback_result = callback_result;

        if (callback_result != 0) {
            g_entries[selected].state = QM_INIT_STATE_FAILED;
            g_running = 0;
            return QM_INIT_ERR_CALLBACK;
        }

        g_entries[selected].state = QM_INIT_STATE_DONE;
        ++completed;
        last_index = selected;
    }

    g_running = 0;
    g_next_stage = (qm_init_stage_t)(stage + 1);

    return QM_INIT_OK;
}

int qm_init_run_all(void)
{
    qm_init_stage_t stage;
    int result;

    for (stage = QM_INIT_STAGE_EARLY;
         stage < QM_INIT_STAGE_COUNT;
         stage = (qm_init_stage_t)(stage + 1)) {

        result = qm_init_run_stage(stage);

        if (result != QM_INIT_OK) {
            return result;
        }
    }

    return QM_INIT_OK;
}

int qm_init_get_state(
    const char *name,
    qm_init_state_t *state
)
{
    int index;

    if (name == NULL || state == NULL) {
        return QM_INIT_ERR_INVALID;
    }

    index = qm_init_find(name);

    if (index < 0) {
        return QM_INIT_ERR_NOT_FOUND;
    }

    *state = g_entries[index].state;
    return QM_INIT_OK;
}

int qm_init_get_callback_result(
    const char *name,
    int *result
)
{
    int index;

    if (name == NULL || result == NULL) {
        return QM_INIT_ERR_INVALID;
    }

    index = qm_init_find(name);

    if (index < 0) {
        return QM_INIT_ERR_NOT_FOUND;
    }

    *result = g_entries[index].callback_result;
    return QM_INIT_OK;
}

size_t qm_init_count(void)
{
    return g_count;
}

const char *qm_init_stage_name(qm_init_stage_t stage)
{
    static const char *const names[] = {
        "EARLY",
        "CORE",
        "SERVICES",
        "LATE"
    };

    if (!qm_init_valid_stage(stage)) {
        return "INVALID";
    }

    return names[stage];
}

const char *qm_init_state_name(qm_init_state_t state)
{
    static const char *const names[] = {
        "PENDING",
        "RUNNING",
        "DONE",
        "FAILED"
    };

    if (state < QM_INIT_STATE_PENDING ||
        state > QM_INIT_STATE_FAILED) {
        return "INVALID";
    }

    return names[state];
}
