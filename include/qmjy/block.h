#ifndef QMJY_BLOCK_H
#define QMJY_BLOCK_H

#include "types.h"
#include "error.h"

typedef qm_u64_t qm_block_device_id_t;

typedef struct qm_block_device_info {
    qm_block_device_id_t id;
    qm_u64_t block_count;
    qm_u32_t block_size;
    qm_u32_t flags;
    const char *name;
} qm_block_device_info_t;

typedef struct qm_block_device_ops {
    qm_status_t (*read)(
        qm_block_device_id_t id,
        qm_u64_t lba,
        qm_u32_t count,
        void *buffer
    );

    qm_status_t (*write)(
        qm_block_device_id_t id,
        qm_u64_t lba,
        qm_u32_t count,
        const void *buffer
    );

    qm_status_t (*flush)(
        qm_block_device_id_t id
    );
} qm_block_device_ops_t;

qm_status_t qm_block_device_register(
    const qm_block_device_info_t *info,
    const qm_block_device_ops_t *ops
);

qm_status_t qm_block_device_unregister(
    qm_block_device_id_t id
);

qm_status_t qm_block_read(
    qm_block_device_id_t id,
    qm_u64_t lba,
    qm_u32_t count,
    void *buffer
);

qm_status_t qm_block_write(
    qm_block_device_id_t id,
    qm_u64_t lba,
    qm_u32_t count,
    const void *buffer
);

#endif
