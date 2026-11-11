#include "qm_service_config.h"

#include <string.h>

static int qm_service_config_find(
    const qm_service_config_entry_t *entries,
    size_t count,
    const char *name
)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        if (!entries[i].enabled || entries[i].name == NULL) {
            continue;
        }

        if (strcmp(entries[i].name, name) == 0) {
            return (int)i;
        }
    }

    return -1;
}

static int qm_service_config_visit(
    const qm_service_config_entry_t *entries,
    size_t count,
    size_t index,
    uint8_t *marks
)
{
    size_t i;
    int dependency_index;

    if (marks[index] == 1) {
        return QM_SERVICE_CONFIG_ERR_CYCLE;
    }

    if (marks[index] == 2) {
        return QM_SERVICE_CONFIG_OK;
    }

    marks[index] = 1;

    for (i = 0; i < entries[index].dependency_count; ++i) {
        dependency_index = qm_service_config_find(
            entries,
            count,
            entries[index].dependencies[i]
        );

        if (dependency_index < 0) {
            return QM_SERVICE_CONFIG_ERR_DEPENDENCY;
        }

        if (qm_service_config_visit(
                entries,
                count,
                (size_t)dependency_index,
                marks) != QM_SERVICE_CONFIG_OK) {
            return QM_SERVICE_CONFIG_ERR_CYCLE;
        }
    }

    marks[index] = 2;
    return QM_SERVICE_CONFIG_OK;
}

int qm_service_config_validate(
    const qm_service_config_entry_t *entries,
    size_t count
)
{
    size_t i;
    size_t j;
    size_t k;
    size_t name_length;
    uint8_t marks[QM_SERVICE_CONFIG_MAX] = {0};

    if ((entries == NULL && count != 0) ||
        count > QM_SERVICE_CONFIG_MAX) {
        return count > QM_SERVICE_CONFIG_MAX
            ? QM_SERVICE_CONFIG_ERR_FULL
            : QM_SERVICE_CONFIG_ERR_INVALID;
    }

    for (i = 0; i < count; ++i) {
        const qm_service_config_entry_t *entry = &entries[i];

        if (!entry->enabled) {
            continue;
        }

        if (entry->name == NULL || entry->start == NULL ||
            entry->dependency_count > QM_SERVICE_DEPS_MAX ||
            (entry->dependency_count != 0 &&
             entry->dependencies == NULL)) {
            return QM_SERVICE_CONFIG_ERR_INVALID;
        }

        name_length = strlen(entry->name);

        if (name_length == 0 ||
            name_length >= QM_SERVICE_NAME_MAX) {
            return QM_SERVICE_CONFIG_ERR_INVALID;
        }

        for (j = 0; j < i; ++j) {
            if (entries[j].enabled &&
                entries[j].name != NULL &&
                strcmp(entry->name, entries[j].name) == 0) {
                return QM_SERVICE_CONFIG_ERR_DUPLICATE;
            }
        }

        for (j = 0; j < entry->dependency_count; ++j) {
            const char *dependency = entry->dependencies[j];

            if (dependency == NULL || dependency[0] == '\0') {
                return QM_SERVICE_CONFIG_ERR_INVALID;
            }

            if (strcmp(entry->name, dependency) == 0) {
                return QM_SERVICE_CONFIG_ERR_CYCLE;
            }

            for (k = 0; k < j; ++k) {
                if (strcmp(entry->dependencies[k], dependency) == 0) {
                    return QM_SERVICE_CONFIG_ERR_DUPLICATE;
                }
            }

            if (qm_service_config_find(
                    entries, count, dependency) < 0) {
                return QM_SERVICE_CONFIG_ERR_DEPENDENCY;
            }
        }
    }

    for (i = 0; i < count; ++i) {
        if (!entries[i].enabled) {
            continue;
        }

        if (qm_service_config_visit(
                entries, count, i, marks) != QM_SERVICE_CONFIG_OK) {
            return QM_SERVICE_CONFIG_ERR_CYCLE;
        }
    }

    return QM_SERVICE_CONFIG_OK;
}

int qm_service_config_apply(
    const qm_service_config_entry_t *entries,
    size_t count
)
{
    size_t i;
    int result;

    result = qm_service_config_validate(entries, count);

    if (result != QM_SERVICE_CONFIG_OK) {
        return result;
    }

    /*
     * The current service manager has no transactional unregister-all
     * operation. Require an empty manager to avoid partial application
     * on top of an existing service set.
     */
    if (qm_service_count() != 0) {
        return QM_SERVICE_CONFIG_ERR_STATE;
    }

    for (i = 0; i < count; ++i) {
        const qm_service_config_entry_t *entry = &entries[i];

        if (!entry->enabled) {
            continue;
        }

        result = qm_service_register(
            entry->name,
            entry->start,
            entry->stop,
            entry->dependencies,
            entry->dependency_count
        );

        if (result != QM_SERVICE_OK) {
            return QM_SERVICE_CONFIG_ERR_REGISTER;
        }
    }

    return QM_SERVICE_CONFIG_OK;
}
