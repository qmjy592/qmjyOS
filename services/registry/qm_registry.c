#include "qm_registry.h"

typedef struct {
    int active;
    char name[QM_REGISTRY_NAME_MAX];
    qm_registry_endpoint_t endpoint;
} qm_registry_entry_t;

static qm_registry_entry_t g_entries[QM_REGISTRY_MAX];
static size_t g_count;

static size_t qm_registry_strlen(const char *s)
{
    size_t n = 0;

    if (!s)
        return 0;

    while (s[n])
        ++n;

    return n;
}

static int qm_registry_strcmp(const char *a, const char *b)
{
    size_t i = 0;

    if (!a || !b)
        return -1;

    while (a[i] && b[i]) {
        if (a[i] != b[i])
            return (unsigned char)a[i] - (unsigned char)b[i];
        ++i;
    }

    return (unsigned char)a[i] - (unsigned char)b[i];
}

static int qm_registry_find(const char *name)
{
    size_t i;

    for (i = 0; i < QM_REGISTRY_MAX; ++i) {
        if (g_entries[i].active &&
            qm_registry_strcmp(g_entries[i].name, name) == 0)
            return (int)i;
    }

    return -1;
}

void qm_registry_init(void)
{
    size_t i;

    for (i = 0; i < QM_REGISTRY_MAX; ++i) {
        g_entries[i].active = 0;
        g_entries[i].name[0] = '\0';
        g_entries[i].endpoint = 0;
    }

    g_count = 0;
}

int qm_registry_register(
    const char *name,
    qm_registry_endpoint_t endpoint
)
{
    size_t i;
    size_t length;

    if (!name || !name[0] || endpoint == 0)
        return QM_REGISTRY_ERR_INVALID;

    length = qm_registry_strlen(name);

    if (length >= QM_REGISTRY_NAME_MAX)
        return QM_REGISTRY_ERR_INVALID;

    if (qm_registry_find(name) >= 0)
        return QM_REGISTRY_ERR_EXISTS;

    for (i = 0; i < QM_REGISTRY_MAX; ++i) {
        if (!g_entries[i].active)
            break;
    }

    if (i == QM_REGISTRY_MAX)
        return QM_REGISTRY_ERR_FULL;

    g_entries[i].active = 1;

    for (size_t j = 0; j <= length; ++j)
        g_entries[i].name[j] = name[j];

    g_entries[i].endpoint = endpoint;
    ++g_count;

    return QM_REGISTRY_OK;
}

int qm_registry_unregister(const char *name)
{
    int index;

    if (!name || !name[0])
        return QM_REGISTRY_ERR_INVALID;

    index = qm_registry_find(name);

    if (index < 0)
        return QM_REGISTRY_ERR_NOT_FOUND;

    g_entries[index].active = 0;
    g_entries[index].name[0] = '\0';
    g_entries[index].endpoint = 0;

    --g_count;

    return QM_REGISTRY_OK;
}

int qm_registry_lookup(
    const char *name,
    qm_registry_endpoint_t *endpoint
)
{
    int index;

    if (!name || !name[0] || !endpoint)
        return QM_REGISTRY_ERR_INVALID;

    index = qm_registry_find(name);

    if (index < 0)
        return QM_REGISTRY_ERR_NOT_FOUND;

    *endpoint = g_entries[index].endpoint;

    return QM_REGISTRY_OK;
}

size_t qm_registry_count(void)
{
    return g_count;
}
