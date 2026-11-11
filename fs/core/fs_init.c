#include "fs_init.h"

#include "../vfs/vfs.h"
#include "../ramfs/ramfs.h"

int fs_init(void)
{
    int result;

    result = ramfs_init();

    if (result != VFS_OK)
        return result;

    result = vfs_init(ramfs_root());

    if (result != VFS_OK)
        return result;

    result = vfs_register_filesystem(ramfs_filesystem());

    if (result != VFS_OK)
        return result;

    return VFS_OK;
}
