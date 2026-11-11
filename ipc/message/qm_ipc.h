#ifndef QM_IPC_H
#define QM_IPC_H

#include <stddef.h>
#include <stdint.h>

#define QM_IPC_MAX_ENDPOINTS 32
#define QM_IPC_QUEUE_DEPTH  16
#define QM_IPC_DATA_SIZE    64

typedef uint32_t qm_ipc_endpoint_t;

typedef enum {
    QM_IPC_OK = 0,
    QM_IPC_ERR_INVALID = -1,
    QM_IPC_ERR_FULL = -2,
    QM_IPC_ERR_NOT_FOUND = -3,
    QM_IPC_ERR_EMPTY = -4,
    QM_IPC_ERR_TOO_LARGE = -5,
    QM_IPC_ERR_EXISTS = -6
} qm_ipc_error_t;

typedef struct {
    qm_ipc_endpoint_t sender;
    uint32_t type;
    uint32_t length;
    uint8_t data[QM_IPC_DATA_SIZE];
} qm_ipc_message_t;

void qm_ipc_init(void);

int qm_ipc_endpoint_create(qm_ipc_endpoint_t *endpoint);

int qm_ipc_endpoint_destroy(qm_ipc_endpoint_t endpoint);

int qm_ipc_send(
    qm_ipc_endpoint_t sender,
    qm_ipc_endpoint_t receiver,
    uint32_t type,
    const void *data,
    size_t length
);

int qm_ipc_receive(
    qm_ipc_endpoint_t receiver,
    qm_ipc_message_t *message
);

size_t qm_ipc_pending(qm_ipc_endpoint_t endpoint);

size_t qm_ipc_endpoint_count(void);

#endif
