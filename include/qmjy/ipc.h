#ifndef QMJY_IPC_H
#define QMJY_IPC_H

#include "types.h"
#include "error.h"

typedef qm_u64_t qm_ipc_endpoint_t;
typedef qm_u64_t qm_ipc_handle_t;

#define QM_IPC_INLINE_MAX 64U

typedef struct qm_ipc_message {
    qm_u32_t type;
    qm_u32_t flags;
    qm_u64_t sequence;
    qm_size_t payload_size;
    qm_u8_t payload[QM_IPC_INLINE_MAX];
} qm_ipc_message_t;

qm_status_t qm_ipc_endpoint_create(
    qm_ipc_endpoint_t *out_endpoint
);

qm_status_t qm_ipc_endpoint_close(
    qm_ipc_endpoint_t endpoint
);

qm_status_t qm_ipc_send(
    qm_ipc_endpoint_t endpoint,
    const qm_ipc_message_t *message
);

qm_status_t qm_ipc_receive(
    qm_ipc_endpoint_t endpoint,
    qm_ipc_message_t *message,
    qm_u64_t timeout_ns
);

#endif
