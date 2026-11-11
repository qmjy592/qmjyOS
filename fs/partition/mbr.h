#ifndef QMJY_MBR_H
#define QMJY_MBR_H

#include "../../include/qmjy/block.h"

#define QM_MBR_PARTITION_COUNT 4
#define QM_MBR_SECTOR_SIZE 512

typedef struct qm_partition {
    qm_block_device_id_t device_id;
    qm_u32_t index;
    qm_u8_t type;
    qm_u8_t bootable;
    qm_u64_t start_lba;
    qm_u64_t sector_count;
} qm_partition_t;

/*
 * Scan the MBR partition table.
 *
 * out_partitions:
 *   Caller-provided output array.
 *
 * capacity:
 *   Number of entries available in the output array.
 *
 * out_count:
 *   Number of valid primary partitions returned.
 *
 * Return:
 *   0  success
 *  -1  invalid argument or I/O failure
 *  -2  invalid MBR signature
 *  -3  invalid partition entry
 */
int qm_mbr_scan(
    qm_block_device_id_t device_id,
    qm_partition_t *out_partitions,
    qm_u32_t capacity,
    qm_u32_t *out_count
);

#endif
