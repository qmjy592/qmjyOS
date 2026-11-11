#include "fs_init.h"

#include "../vfs/vfs.h"
#include "../ramfs/ramfs.h"
#include "../qfs/qfs.h"

int fs_init(void)
{
    int result;
    struct vfs_node *disk_mountpoint;

    result = ramfs_init();

    if (result != VFS_OK)
        return result;

    result = vfs_init(ramfs_root());

    if (result != VFS_OK)
        return result;

    result = vfs_register_filesystem(ramfs_filesystem());

    if (result != VFS_OK)
        return result;

    result = vfs_register_filesystem(qfs_filesystem());

    if (result != VFS_OK)
        return result;

    result = vfs_create(
        ramfs_root(),
        "disk",
        VFS_NODE_DIRECTORY,
        &disk_mountpoint);

    if (result != VFS_OK && result != VFS_ERR_EXISTS)
        return result;

    return VFS_OK;
}
