#ifndef QMJY_FS_H
#define QMJY_FS_H

#include "types.h"
#include "error.h"

typedef qm_u64_t qm_mount_id_t;
typedef qm_u64_t qm_file_handle_t;

typedef struct qm_vnode qm_vnode_t;
typedef struct qm_file qm_file_t;

typedef enum qm_file_type {
    QM_FILE_UNKNOWN = 0,
    QM_FILE_REGULAR,
    QM_FILE_DIRECTORY,
    QM_FILE_SYMLINK,
    QM_FILE_DEVICE,
    QM_FILE_FIFO,
    QM_FILE_SOCKET
} qm_file_type_t;

typedef enum qm_file_flags {
    QM_FILE_READ       = 1U << 0,
    QM_FILE_WRITE      = 1U << 1,
    QM_FILE_CREATE     = 1U << 2,
    QM_FILE_EXCLUSIVE  = 1U << 3,
    QM_FILE_TRUNCATE   = 1U << 4,
    QM_FILE_APPEND     = 1U << 5
} qm_file_flags_t;

typedef struct qm_file_info {
    qm_file_type_t type;
    qm_u64_t size;
    qm_u64_t inode;
    qm_u32_t permissions;
    qm_u32_t flags;
} qm_file_info_t;

typedef struct qm_fs_type {
    const char *name;

    qm_status_t (*mount)(
        const char *source,
        const char *target,
        const void *options,
        qm_mount_id_t *out_mount
    );

    qm_status_t (*unmount)(
        qm_mount_id_t mount
    );
} qm_fs_type_t;

qm_status_t qm_fs_register(
    const qm_fs_type_t *filesystem
);

qm_status_t qm_fs_mount(
    const char *filesystem_type,
    const char *source,
    const char *target,
    const void *options,
    qm_mount_id_t *out_mount
);

qm_status_t qm_fs_unmount(
    qm_mount_id_t mount
);

qm_status_t qm_fs_open(
    const char *path,
    qm_u32_t flags,
    qm_file_handle_t *out_handle
);

qm_status_t qm_fs_close(
    qm_file_handle_t handle
);

qm_status_t qm_fs_read(
    qm_file_handle_t handle,
    void *buffer,
    qm_size_t size,
    qm_size_t *bytes_read
);

qm_status_t qm_fs_write(
    qm_file_handle_t handle,
    const void *buffer,
    qm_size_t size,
    qm_size_t *bytes_written
);

qm_status_t qm_fs_seek(
    qm_file_handle_t handle,
    qm_s64_t offset,
    qm_u32_t origin,
    qm_u64_t *new_position
);

qm_status_t qm_fs_stat(
    const char *path,
    qm_file_info_t *info
);

qm_status_t qm_fs_mkdir(
    const char *path,
    qm_u32_t permissions
);

qm_status_t qm_fs_unlink(
    const char *path
);

#endif
