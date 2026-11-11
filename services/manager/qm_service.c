#include "qm_service.h"

static qm_service_t g_services[QM_SERVICE_MAX];

/*
 * Dependency names are owned by the manager.
 * The public qm_service_t layout remains unchanged.
 */
static char g_dependency_names[
    QM_SERVICE_MAX
][QM_SERVICE_DEPS_MAX][QM_SERVICE_NAME_MAX];

static size_t g_service_count;

static size_t qm_strlen(const char *s)
{
    size_t n = 0;

    if (!s)
        return 0;

    while (s[n])
        ++n;

    return n;
}

static int qm_strcmp(const char *a, const char *b)
{
    size_t i = 0;

    if (!a || !b)
        return -1;

    while (a[i] && b[i]) {
        if (a[i] != b[i])
            return (unsigned char)a[i] -
                   (unsigned char)b[i];

        ++i;
    }

    return (unsigned char)a[i] -
           (unsigned char)b[i];
}

static void qm_strcopy(
    char *dst,
    size_t capacity,
    const char *src
)
{
    size_t i = 0;

    if (!dst || !capacity)
        return;

    while (src && src[i] && i + 1 < capacity) {
        dst[i] = src[i];
        ++i;
    }

    dst[i] = '\0';
}

static int qm_service_find(const char *name)
{
    size_t i;

    if (!name || !name[0])
        return -1;

    for (i = 0; i < g_service_count; ++i) {
        if (qm_strcmp(g_services[i].name, name) == 0)
            return (int)i;
    }

    return -1;
}

static int qm_service_is_active(const qm_service_t *service)
{
    return service->state == QM_SERVICE_RUNNING ||
           service->state == QM_SERVICE_FAILED;
}

static int qm_service_dependencies_ready(
    const qm_service_t *service
)
{
    uint8_t i;

    for (i = 0; i < service->dependency_count; ++i) {
        int index = qm_service_find(
            service->dependencies[i]
        );

        if (index < 0)
            return QM_SERVICE_ERR_DEPENDENCY;

        if (g_services[index].state != QM_SERVICE_RUNNING)
            return QM_SERVICE_ERR_DEPENDENCY;
    }

    return QM_SERVICE_OK;
}

/*
 * Stable Kahn topological ordering.
 * When multiple services are ready, registration order wins.
 */
static int qm_service_build_order(
    size_t order[QM_SERVICE_MAX],
    size_t *out_count
)
{
    uint8_t indegree[QM_SERVICE_MAX] = {0};
    uint8_t selected[QM_SERVICE_MAX] = {0};

    size_t i;
    size_t j;
    size_t k;
    size_t count = 0;

    if (!out_count)
        return QM_SERVICE_ERR_INVALID;

    for (i = 0; i < g_service_count; ++i) {
        indegree[i] = g_services[i].dependency_count;

        for (j = 0; j < g_services[i].dependency_count; ++j) {
            if (qm_service_find(
                    g_services[i].dependencies[j]) < 0) {
                return QM_SERVICE_ERR_DEPENDENCY;
            }
        }
    }

    while (count < g_service_count) {
        size_t pick = g_service_count;

        for (i = 0; i < g_service_count; ++i) {
            if (!selected[i] && indegree[i] == 0) {
                pick = i;
                break;
            }
        }

        if (pick == g_service_count)
            return QM_SERVICE_ERR_CYCLE;

        selected[pick] = 1;
        order[count++] = pick;

        for (i = 0; i < g_service_count; ++i) {
            if (selected[i])
                continue;

            for (k = 0; k < g_services[i].dependency_count; ++k) {
                if (qm_strcmp(
                        g_services[i].dependencies[k],
                        g_services[pick].name) == 0) {
                    if (indegree[i] > 0)
                        --indegree[i];

                    break;
                }
            }
        }
    }

    *out_count = count;
    return QM_SERVICE_OK;
}

void qm_service_manager_init(void)
{
    size_t i;
    size_t j;
    size_t k;

    for (i = 0; i < QM_SERVICE_MAX; ++i) {
        g_services[i].name[0] = '\0';
        g_services[i].start = NULL;
        g_services[i].stop = NULL;
        g_services[i].dependency_count = 0;
        g_services[i].state = QM_SERVICE_EMPTY;
        g_services[i].last_error = 0;

        for (j = 0; j < QM_SERVICE_DEPS_MAX; ++j) {
            g_services[i].dependencies[j] = NULL;

            for (k = 0; k < QM_SERVICE_NAME_MAX; ++k)
                g_dependency_names[i][j][k] = '\0';
        }
    }

    g_service_count = 0;
}

int qm_service_register(
    const char *name,
    qm_service_start_fn start,
    qm_service_stop_fn stop,
    const char *const *dependencies,
    uint8_t dependency_count
)
{
    qm_service_t *service;
    size_t i;
    size_t j;

    if (!name || !name[0] || !start)
        return QM_SERVICE_ERR_INVALID;

    if (qm_strlen(name) >= QM_SERVICE_NAME_MAX)
        return QM_SERVICE_ERR_INVALID;

    if (dependency_count > QM_SERVICE_DEPS_MAX)
        return QM_SERVICE_ERR_INVALID;

    if (dependency_count && !dependencies)
        return QM_SERVICE_ERR_INVALID;

    if (qm_service_find(name) >= 0)
        return QM_SERVICE_ERR_DUPLICATE;

    if (g_service_count >= QM_SERVICE_MAX)
        return QM_SERVICE_ERR_FULL;

    for (i = 0; i < dependency_count; ++i) {
        if (!dependencies[i] || !dependencies[i][0])
            return QM_SERVICE_ERR_INVALID;

        if (qm_strlen(dependencies[i]) >= QM_SERVICE_NAME_MAX)
            return QM_SERVICE_ERR_INVALID;

        if (qm_strcmp(name, dependencies[i]) == 0)
            return QM_SERVICE_ERR_CYCLE;

        for (j = 0; j < i; ++j) {
            if (qm_strcmp(
                    dependencies[i],
                    dependencies[j]) == 0) {
                return QM_SERVICE_ERR_DUPLICATE;
            }
        }
    }

    service = &g_services[g_service_count];

    qm_strcopy(
        service->name,
        sizeof(service->name),
        name
    );

    service->start = start;
    service->stop = stop;
    service->dependency_count = dependency_count;
    service->state = QM_SERVICE_REGISTERED;
    service->last_error = 0;

    for (i = 0; i < QM_SERVICE_DEPS_MAX; ++i) {
        service->dependencies[i] = NULL;

        if (i < dependency_count) {
            qm_strcopy(
                g_dependency_names[g_service_count][i],
                QM_SERVICE_NAME_MAX,
                dependencies[i]
            );

            service->dependencies[i] =
                g_dependency_names[g_service_count][i];
        }
    }

    ++g_service_count;

    return QM_SERVICE_OK;
}

int qm_service_start(const char *name)
{
    int index;
    int result;
    qm_service_t *service;

    index = qm_service_find(name);

    if (index < 0)
        return QM_SERVICE_ERR_NOT_FOUND;

    service = &g_services[index];

    if (service->state == QM_SERVICE_RUNNING)
        return QM_SERVICE_OK;

    if (service->state == QM_SERVICE_STARTING ||
        service->state == QM_SERVICE_STOPPING)
        return QM_SERVICE_ERR_STATE;

    if (service->state != QM_SERVICE_REGISTERED &&
        service->state != QM_SERVICE_STOPPED)
        return QM_SERVICE_ERR_STATE;

    result = qm_service_dependencies_ready(service);

    if (result != QM_SERVICE_OK) {
        service->last_error = result;
        return result;
    }

    service->state = QM_SERVICE_STARTING;

    result = service->start();

    if (result != 0) {
        service->state = QM_SERVICE_FAILED;
        service->last_error = result;
        return QM_SERVICE_ERR_CALLBACK;
    }

    service->state = QM_SERVICE_RUNNING;
    service->last_error = 0;

    return QM_SERVICE_OK;
}

static int qm_service_has_active_dependents(size_t target)
{
    size_t i;
    uint8_t j;

    for (i = 0; i < g_service_count; ++i) {
        if (i == target || !qm_service_is_active(&g_services[i]))
            continue;

        for (j = 0; j < g_services[i].dependency_count; ++j) {
            if (qm_strcmp(
                    g_services[i].dependencies[j],
                    g_services[target].name) == 0) {
                return 1;
            }
        }
    }

    return 0;
}

int qm_service_stop(const char *name)
{
    int index;
    int result;
    qm_service_t *service;

    index = qm_service_find(name);

    if (index < 0)
        return QM_SERVICE_ERR_NOT_FOUND;

    service = &g_services[index];

    if (service->state == QM_SERVICE_STOPPED ||
        service->state == QM_SERVICE_REGISTERED)
        return QM_SERVICE_OK;

    if (service->state != QM_SERVICE_RUNNING &&
        service->state != QM_SERVICE_FAILED)
        return QM_SERVICE_ERR_STATE;

    if (qm_service_has_active_dependents((size_t)index))
        return QM_SERVICE_ERR_IN_USE;

    service->state = QM_SERVICE_STOPPING;

    if (service->stop) {
        result = service->stop();

        if (result != 0) {
            service->state = QM_SERVICE_FAILED;
            service->last_error = result;
            return QM_SERVICE_ERR_CALLBACK;
        }
    }

    service->state = QM_SERVICE_STOPPED;
    service->last_error = 0;

    return QM_SERVICE_OK;
}

int qm_service_start_all(void)
{
    size_t order[QM_SERVICE_MAX];
    size_t count = 0;
    size_t i;
    int result;

    result = qm_service_build_order(order, &count);

    if (result != QM_SERVICE_OK)
        return result;

    for (i = 0; i < count; ++i) {
        qm_service_t *service = &g_services[order[i]];

        if (service->state == QM_SERVICE_RUNNING)
            continue;

        if (service->state != QM_SERVICE_REGISTERED &&
            service->state != QM_SERVICE_STOPPED) {
            return QM_SERVICE_ERR_STATE;
        }

        result = qm_service_start(service->name);

        if (result != QM_SERVICE_OK)
            return result;
    }

    return QM_SERVICE_OK;
}

int qm_service_stop_all(void)
{
    size_t order[QM_SERVICE_MAX];
    size_t count = 0;
    size_t i;
    int result;
    int first_error = QM_SERVICE_OK;

    result = qm_service_build_order(order, &count);

    if (result != QM_SERVICE_OK)
        return result;

    for (i = count; i > 0; --i) {
        qm_service_t *service = &g_services[order[i - 1]];

        if (!qm_service_is_active(service))
            continue;

        result = qm_service_stop(service->name);

        if (result != QM_SERVICE_OK &&
            first_error == QM_SERVICE_OK) {
            first_error = result;
        }
    }

    return first_error;
}

int qm_service_get_state(
    const char *name,
    qm_service_state_t *state
)
{
    int index;

    if (!state)
        return QM_SERVICE_ERR_INVALID;

    index = qm_service_find(name);

    if (index < 0)
        return QM_SERVICE_ERR_NOT_FOUND;

    *state = g_services[index].state;
    return QM_SERVICE_OK;
}

int qm_service_get_error(
    const char *name,
    int *error
)
{
    int index;

    if (!error)
        return QM_SERVICE_ERR_INVALID;

    index = qm_service_find(name);

    if (index < 0)
        return QM_SERVICE_ERR_NOT_FOUND;

    *error = g_services[index].last_error;
    return QM_SERVICE_OK;
}

size_t qm_service_count(void)
{
    return g_service_count;
}

const char *qm_service_state_name(qm_service_state_t state)
{
    switch (state) {
    case QM_SERVICE_EMPTY:
        return "EMPTY";
    case QM_SERVICE_REGISTERED:
        return "REGISTERED";
    case QM_SERVICE_STARTING:
        return "STARTING";
    case QM_SERVICE_RUNNING:
        return "RUNNING";
    case QM_SERVICE_STOPPING:
        return "STOPPING";
    case QM_SERVICE_STOPPED:
        return "STOPPED";
    case QM_SERVICE_FAILED:
        return "FAILED";
    default:
        return "UNKNOWN";
    }
}
