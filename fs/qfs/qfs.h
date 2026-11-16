#ifndef QMJY_QFS_H
#define QMJY_QFS_H

#include "../vfs/vfs.h"
#include "../../include/qmjy/block.h"
#include "../partition/mbr.h"

#define QFS_VERSION          2U
#define QFS_LEGACY_VERSION   1U
#define QFS_LEGACY_DATA_START 25U
#define QFS_JOURNAL_START    25U
#define QFS_JOURNAL_SECTORS 13U
#define QFS_JOURNAL_MAX_ENTRIES 10U
#define QFS_DATA_START      38U
#define QFS_MAX_INODES       64U
#define QFS_MAX_NAME         60U
#define QFS_MAX_FILE_SIZE    4096U
#define QFS_MAX_SECTORS      4096U
#define QFS_METADATA_SECTORS 38U

/*
 * QFS v2 uses a checksummed redo journal in sectors 25-37.
 * QFS v1 keeps its original on-disk layout and is mounted read-only.
 * Both versions use one flat root directory and files limited to 4 KiB.
 * Partitions contain 64-4096 sectors.
 */

#ifdef QFS_TESTING
void qfs_test_reset_state(void);
#endif

int qfs_format(const qm_partition_t *partition);
int qfs_mount(const qm_partition_t *partition);
int qfs_mount_at(
    const qm_partition_t *partition,
    const char *target
);

int qfs_unmount(void);

struct vfs_node *qfs_root(void);
const struct vfs_filesystem *qfs_filesystem(void);

#endif
