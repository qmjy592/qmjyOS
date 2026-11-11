#include "fs_storage.h"

#include "../../drivers/block/ide_pio.h"
#include "../partition/mbr.h"
#include "../qfs/qfs.h"
#include "../vfs/vfs.h"

#define FS_STORAGE_MAX_PARTITIONS 4U
#define FS_STORAGE_QFS_TYPE       0x83U

int fs_storage_init(void)
{
    qm_partition_t partitions[FS_STORAGE_MAX_PARTITIONS];
    qm_u32_t partition_count = 0;
    qm_u32_t i;
    int result;

    result = qm_ide_pio_init();

    if (result != 0)
        return VFS_ERR_NOT_FOUND;

    result = qm_mbr_scan(
        QM_IDE_PRIMARY_SLAVE,
        partitions,
        FS_STORAGE_MAX_PARTITIONS,
        &partition_count);

    if (result != 0)
        return VFS_ERR_NOT_FOUND;

    for (i = 0; i < partition_count; ++i) {
        if (partitions[i].type != FS_STORAGE_QFS_TYPE)
            continue;

        result = qfs_mount_at(&partitions[i], "/disk");

        if (result == VFS_OK)
            return VFS_OK;
    }

    return VFS_ERR_NOT_FOUND;
}
