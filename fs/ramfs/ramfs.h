#ifndef QMJYOS_RAMFS_H
#define QMJYOS_RAMFS_H

#include "../vfs/vfs.h"

#define RAMFS_MAX_NODES       64
#define RAMFS_MAX_NAME        64
#define RAMFS_MAX_FILE_SIZE   4096

int ramfs_init(void);
struct vfs_node *ramfs_root(void);
const struct vfs_filesystem *ramfs_filesystem(void);

#endif
