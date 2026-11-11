#include "../../include/qmjy/block.h"

#define QM_BLOCK_MAX_DEVICES 8

struct qm_block_device_entry {
    qm_block_device_info_t info;
    qm_block_device_ops_t ops;
    qm_u8_t used;
};

static struct qm_block_device_entry devices[QM_BLOCK_MAX_DEVICES];

static int block_find(qm_block_device_id_t id)
{
    qm_u32_t i;

    for (i = 0; i < QM_BLOCK_MAX_DEVICES; ++i) {
        if (devices[i].used && devices[i].info.id == id)
            return (int)i;
    }

    return -1;
}

qm_status_t qm_block_device_register(
    const qm_block_device_info_t *info,
    const qm_block_device_ops_t *ops)
{
    qm_u32_t i;

    if (info == 0 || ops == 0)
        return (qm_status_t)-1;

    if (info->id == 0 || info->block_count == 0 ||
        info->block_size == 0 || ops->read == 0)
        return (qm_status_t)-1;

    if (block_find(info->id) >= 0)
        return (qm_status_t)-2;

    for (i = 0; i < QM_BLOCK_MAX_DEVICES; ++i) {
        if (!devices[i].used) {
            devices[i].info = *info;
            devices[i].ops = *ops;
            devices[i].used = 1;
            return (qm_status_t)0;
        }
    }

    return (qm_status_t)-3;
}

qm_status_t qm_block_device_unregister(qm_block_device_id_t id)
{
    int index = block_find(id);

    if (index < 0)
        return (qm_status_t)-1;

    devices[index].used = 0;
    devices[index].info.id = 0;
    devices[index].info.block_count = 0;
    devices[index].info.block_size = 0;
    devices[index].info.flags = 0;
    devices[index].info.name = 0;

    devices[index].ops.read = 0;
    devices[index].ops.write = 0;
    devices[index].ops.flush = 0;

    return (qm_status_t)0;
}

qm_status_t qm_block_read(
    qm_block_device_id_t id,
    qm_u64_t lba,
    qm_u32_t count,
    void *buffer)
{
    int index = block_find(id);
    const qm_block_device_info_t *info;

    if (index < 0)
        return (qm_status_t)-1;

    info = &devices[index].info;

    if (count == 0)
        return (qm_status_t)0;

    if (buffer == 0 || lba >= info->block_count)
        return (qm_status_t)-1;

    if ((qm_u64_t)count > info->block_count - lba)
        return (qm_status_t)-1;

    return devices[index].ops.read(id, lba, count, buffer);
}

qm_status_t qm_block_write(
    qm_block_device_id_t id,
    qm_u64_t lba,
    qm_u32_t count,
    const void *buffer)
{
    int index = block_find(id);
    const qm_block_device_info_t *info;

    if (index < 0)
        return (qm_status_t)-1;

    info = &devices[index].info;

    if (count == 0)
        return (qm_status_t)0;

    if (buffer == 0 || lba >= info->block_count)
        return (qm_status_t)-1;

    if ((qm_u64_t)count > info->block_count - lba)
        return (qm_status_t)-1;

    if (devices[index].ops.write == 0)
        return (qm_status_t)-4;

    return devices[index].ops.write(id, lba, count, buffer);
}


qm_status_t qm_block_flush(qm_block_device_id_t id)
{
    int index = block_find(id);

    if (index < 0)
        return (qm_status_t)-1;

    if (devices[index].ops.flush == 0)
        return (qm_status_t)0;

    return devices[index].ops.flush(id);
}


qm_status_t qm_block_get_info(
    qm_block_device_id_t id,
    qm_block_device_info_t *out_info)
{
    int index;

    if (out_info == 0)
        return (qm_status_t)-1;

    index = block_find(id);

    if (index < 0)
        return (qm_status_t)-1;

    *out_info = devices[index].info;
    return (qm_status_t)0;
}
