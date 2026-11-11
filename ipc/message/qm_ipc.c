#include "qm_ipc.h"

typedef struct {
    int active;
    qm_ipc_endpoint_t id;

    qm_ipc_message_t queue[QM_IPC_QUEUE_DEPTH];

    size_t head;
    size_t tail;
    size_t count;
} qm_ipc_endpoint_state_t;

static qm_ipc_endpoint_state_t
    g_endpoints[QM_IPC_MAX_ENDPOINTS];

static qm_ipc_endpoint_t g_next_id;
static size_t g_endpoint_count;

static qm_ipc_endpoint_state_t *
qm_ipc_find(qm_ipc_endpoint_t id)
{
    size_t i;

    if (id == 0)
        return NULL;

    for (i = 0; i < QM_IPC_MAX_ENDPOINTS; ++i) {
        if (g_endpoints[i].active &&
            g_endpoints[i].id == id) {
            return &g_endpoints[i];
        }
    }

    return NULL;
}

void qm_ipc_init(void)
{
    size_t i;

    for (i = 0; i < QM_IPC_MAX_ENDPOINTS; ++i) {
        g_endpoints[i].active = 0;
        g_endpoints[i].id = 0;
        g_endpoints[i].head = 0;
        g_endpoints[i].tail = 0;
        g_endpoints[i].count = 0;
    }

    g_next_id = 1;
    g_endpoint_count = 0;
}

int qm_ipc_endpoint_create(qm_ipc_endpoint_t *endpoint)
{
    size_t i;
    qm_ipc_endpoint_state_t *slot = NULL;

    if (!endpoint)
        return QM_IPC_ERR_INVALID;

    for (i = 0; i < QM_IPC_MAX_ENDPOINTS; ++i) {
        if (!g_endpoints[i].active) {
            slot = &g_endpoints[i];
            break;
        }
    }

    if (!slot)
        return QM_IPC_ERR_FULL;

    /*
     * IDs are monotonically allocated.
     * Zero is reserved as invalid.
     */
    if (g_next_id == 0)
        return QM_IPC_ERR_FULL;

    slot->id = g_next_id++;

    slot->head = 0;
    slot->tail = 0;
    slot->count = 0;
    slot->active = 1;

    ++g_endpoint_count;

    *endpoint = slot->id;

    return QM_IPC_OK;
}

int qm_ipc_endpoint_destroy(qm_ipc_endpoint_t endpoint)
{
    qm_ipc_endpoint_state_t *slot;
    size_t i;

    slot = qm_ipc_find(endpoint);

    if (!slot)
        return QM_IPC_ERR_NOT_FOUND;

    slot->active = 0;
    slot->id = 0;
    slot->head = 0;
    slot->tail = 0;
    slot->count = 0;

    for (i = 0; i < QM_IPC_QUEUE_DEPTH; ++i) {
        slot->queue[i].sender = 0;
        slot->queue[i].type = 0;
        slot->queue[i].length = 0;
    }

    --g_endpoint_count;

    return QM_IPC_OK;
}

int qm_ipc_send(
    qm_ipc_endpoint_t sender,
    qm_ipc_endpoint_t receiver,
    uint32_t type,
    const void *data,
    size_t length
)
{
    qm_ipc_endpoint_state_t *source;
    qm_ipc_endpoint_state_t *destination;
    qm_ipc_message_t *message;
    size_t i;

    if (length > QM_IPC_DATA_SIZE)
        return QM_IPC_ERR_TOO_LARGE;

    if (length > 0 && !data)
        return QM_IPC_ERR_INVALID;

    source = qm_ipc_find(sender);
    destination = qm_ipc_find(receiver);

    if (!source || !destination)
        return QM_IPC_ERR_NOT_FOUND;

    if (destination->count >= QM_IPC_QUEUE_DEPTH)
        return QM_IPC_ERR_FULL;

    message = &destination->queue[destination->tail];

    message->sender = sender;
    message->type = type;
    message->length = (uint32_t)length;

    for (i = 0; i < length; ++i)
        message->data[i] = ((const uint8_t *)data)[i];

    destination->tail =
        (destination->tail + 1) % QM_IPC_QUEUE_DEPTH;

    ++destination->count;

    return QM_IPC_OK;
}

int qm_ipc_receive(
    qm_ipc_endpoint_t receiver,
    qm_ipc_message_t *message
)
{
    qm_ipc_endpoint_state_t *destination;
    qm_ipc_message_t *queued;
    size_t i;

    if (!message)
        return QM_IPC_ERR_INVALID;

    destination = qm_ipc_find(receiver);

    if (!destination)
        return QM_IPC_ERR_NOT_FOUND;

    if (destination->count == 0)
        return QM_IPC_ERR_EMPTY;

    queued = &destination->queue[destination->head];

    message->sender = queued->sender;
    message->type = queued->type;
    message->length = queued->length;

    for (i = 0; i < queued->length; ++i)
        message->data[i] = queued->data[i];

    destination->head =
        (destination->head + 1) % QM_IPC_QUEUE_DEPTH;

    --destination->count;

    return QM_IPC_OK;
}

size_t qm_ipc_pending(qm_ipc_endpoint_t endpoint)
{
    qm_ipc_endpoint_state_t *slot;

    slot = qm_ipc_find(endpoint);

    if (!slot)
        return 0;

    return slot->count;
}

size_t qm_ipc_endpoint_count(void)
{
    return g_endpoint_count;
}
