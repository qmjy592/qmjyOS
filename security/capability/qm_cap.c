#include "qm_cap.h"

#include <string.h>

static qm_cap_entry_t g_entries[QM_CAP_MAX_ENTRIES];
static size_t g_count;

static int qm_cap_valid_rights(qm_cap_rights_t rights)
{
    return rights != 0 && (rights & ~QM_CAP_RIGHT_ALL) == 0;
}

static int qm_cap_find(
    qm_cap_subject_t subject,
    qm_cap_object_t object
)
{
    size_t i;

    for (i = 0; i < g_count; ++i) {
        if (g_entries[i].subject == subject &&
            g_entries[i].object == object) {
            return (int)i;
        }
    }

    return -1;
}

static void qm_cap_remove_index(size_t index)
{
    if (index >= g_count) {
        return;
    }

    if (index + 1 < g_count) {
        memmove(
            &g_entries[index],
            &g_entries[index + 1],
            (g_count - index - 1) * sizeof(g_entries[0])
        );
    }

    --g_count;
    memset(&g_entries[g_count], 0, sizeof(g_entries[0]));
}

void qm_cap_init(void)
{
    memset(g_entries, 0, sizeof(g_entries));
    g_count = 0;
}

int qm_cap_grant(
    qm_cap_subject_t subject,
    qm_cap_object_t object,
    qm_cap_rights_t rights
)
{
    int index;
    qm_cap_entry_t *entry;

    if (subject == 0 || object == 0 ||
        !qm_cap_valid_rights(rights)) {
        return QM_CAP_ERR_INVALID;
    }

    index = qm_cap_find(subject, object);

    if (index >= 0) {
        g_entries[index].rights |= rights;
        return QM_CAP_OK;
    }

    if (g_count >= QM_CAP_MAX_ENTRIES) {
        return QM_CAP_ERR_FULL;
    }

    entry = &g_entries[g_count];

    entry->subject = subject;
    entry->object = object;
    entry->rights = rights;

    ++g_count;
    return QM_CAP_OK;
}

int qm_cap_revoke(
    qm_cap_subject_t subject,
    qm_cap_object_t object,
    qm_cap_rights_t rights
)
{
    int index;

    if (subject == 0 || object == 0 ||
        !qm_cap_valid_rights(rights)) {
        return QM_CAP_ERR_INVALID;
    }

    index = qm_cap_find(subject, object);

    if (index < 0) {
        return QM_CAP_ERR_NOT_FOUND;
    }

    g_entries[index].rights &= ~rights;

    if (g_entries[index].rights == 0) {
        qm_cap_remove_index((size_t)index);
    }

    return QM_CAP_OK;
}

int qm_cap_check(
    qm_cap_subject_t subject,
    qm_cap_object_t object,
    qm_cap_rights_t required
)
{
    int index;

    if (subject == 0 || object == 0 ||
        !qm_cap_valid_rights(required)) {
        return QM_CAP_ERR_INVALID;
    }

    index = qm_cap_find(subject, object);

    if (index < 0) {
        return QM_CAP_ERR_DENIED;
    }

    if ((g_entries[index].rights & required) != required) {
        return QM_CAP_ERR_DENIED;
    }

    return QM_CAP_OK;
}

int qm_cap_get(
    qm_cap_subject_t subject,
    qm_cap_object_t object,
    qm_cap_rights_t *rights
)
{
    int index;

    if (subject == 0 || object == 0 || rights == NULL) {
        return QM_CAP_ERR_INVALID;
    }

    index = qm_cap_find(subject, object);

    if (index < 0) {
        return QM_CAP_ERR_NOT_FOUND;
    }

    *rights = g_entries[index].rights;
    return QM_CAP_OK;
}

int qm_cap_remove(
    qm_cap_subject_t subject,
    qm_cap_object_t object
)
{
    int index;

    if (subject == 0 || object == 0) {
        return QM_CAP_ERR_INVALID;
    }

    index = qm_cap_find(subject, object);

    if (index < 0) {
        return QM_CAP_ERR_NOT_FOUND;
    }

    qm_cap_remove_index((size_t)index);
    return QM_CAP_OK;
}

size_t qm_cap_count(void)
{
    return g_count;
}
