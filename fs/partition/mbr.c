#include "mbr.h"

static qm_u32_t read_le32(const qm_u8_t *p)
{
    return (qm_u32_t)p[0] |
           ((qm_u32_t)p[1] << 8) |
           ((qm_u32_t)p[2] << 16) |
           ((qm_u32_t)p[3] << 24);
}

int qm_mbr_scan(
    qm_block_device_id_t device_id,
    qm_partition_t *out_partitions,
    qm_u32_t capacity,
    qm_u32_t *out_count)
{
    qm_u8_t sector[QM_MBR_SECTOR_SIZE];
    qm_block_device_info_t device_info;
    qm_u32_t found = 0;
    qm_u32_t i;

    if (out_count == 0)
        return -1;

    *out_count = 0;

    if (out_partitions == 0 && capacity != 0)
        return -1;

    if (qm_block_get_info(device_id, &device_info) != 0)
        return -1;

    if (device_info.block_size != QM_MBR_SECTOR_SIZE ||
        device_info.block_count < 1)
        return -1;

    if (qm_block_read(device_id, 0, 1, sector) != 0)
        return -1;

    if (sector[510] != 0x55 || sector[511] != 0xAA)
        return -2;

    for (i = 0; i < QM_MBR_PARTITION_COUNT; ++i) {
        const qm_u8_t *entry = &sector[446 + i * 16];
        qm_u8_t status = entry[0];
        qm_u8_t type = entry[4];
        qm_u32_t start = read_le32(entry + 8);
        qm_u32_t count = read_le32(entry + 12);
        qm_u64_t end;
        qm_u32_t j;

        if (type == 0 && start == 0 && count == 0)
            continue;

        if (status != 0x00 && status != 0x80)
            return -3;

        if (type == 0 || count == 0 || start == 0)
            return -3;

        end = (qm_u64_t)start + (qm_u64_t)count;

        if (end < start || end > device_info.block_count)
            return -3;

        /* Reject overlapping primary partition ranges. */
        for (j = 0; j < found; ++j) {
            qm_u64_t previous_start =
                (qm_u64_t)out_partitions[j].start_lba;
            qm_u64_t previous_end =
                previous_start +
                (qm_u64_t)out_partitions[j].sector_count;

            if ((qm_u64_t)start < previous_end &&
                previous_start < end)
                return -3;
        }


        if (found >= capacity)
            return -3;

        out_partitions[found].device_id = device_id;
        out_partitions[found].index = i;
        out_partitions[found].type = type;
        out_partitions[found].bootable =
            (status == 0x80) ? 1 : 0;
        out_partitions[found].start_lba = start;
        out_partitions[found].sector_count = count;

        ++found;
    }

    *out_count = found;
    return 0;
}
