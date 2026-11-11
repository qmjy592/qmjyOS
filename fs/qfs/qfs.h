#ifndef QMJY_QFS_H
#define QMJY_QFS_H

#include "../vfs/vfs.h"
#include "../../include/qmjy/block.h"
#include "../partition/mbr.h"

#define QFS_VERSION          1U
#define QFS_MAX_INODES       64U
#define QFS_MAX_NAME         60U
#define QFS_MAX_FILE_SIZE    4096U
#define QFS_MAX_SECTORS      4096U
#define QFS_METADATA_SECTORS 25U

/*
 * QFS v0.1 uses one flat root directory.
 * Each file is limited to 4 KiB.
 *
 * The partition must contain at least 64 sectors and
 * no more than 4096 sectors.
 */

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
