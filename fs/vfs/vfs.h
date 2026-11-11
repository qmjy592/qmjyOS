#ifndef QMJYOS_VFS_H
#define QMJYOS_VFS_H

#include <stddef.h>
#include <stdint.h>

#define VFS_MAX_FILESYSTEMS 16
#define VFS_MAX_MOUNT_DEPTH 16

enum vfs_node_type {
    VFS_NODE_UNKNOWN = 0,
    VFS_NODE_FILE,
    VFS_NODE_DIRECTORY,
    VFS_NODE_DEVICE,
    VFS_NODE_SYMLINK
};

enum vfs_error {
    VFS_OK = 0,
    VFS_ERR_INVALID = -1,
    VFS_ERR_NOT_FOUND = -2,
    VFS_ERR_NOT_DIRECTORY = -3,
    VFS_ERR_EXISTS = -4,
    VFS_ERR_NOT_SUPPORTED = -5,
    VFS_ERR_NO_SPACE = -6,
    VFS_ERR_NOT_FILE = -7,
    VFS_ERR_BUSY = -8,
    VFS_ERR_IO = -9
};

struct vfs_node;

struct vfs_operations {
    int (*lookup)(struct vfs_node *directory,
                  const char *name,
                  struct vfs_node **result);

    int (*read)(struct vfs_node *node,
                uint64_t offset,
                void *buffer,
                size_t size);

    int (*write)(struct vfs_node *node,
                 uint64_t offset,
                 const void *buffer,
                 size_t size);

    int (*create)(struct vfs_node *directory,
                  const char *name,
                  enum vfs_node_type type,
                  struct vfs_node **result);

    int (*remove)(struct vfs_node *directory,
                  struct vfs_node *node);
};

struct vfs_node {
    const char *name;
    enum vfs_node_type type;
    uint32_t mode;
    uint64_t size;

    struct vfs_node *parent;
    struct vfs_node *children;
    struct vfs_node *next;

    struct vfs_node *mounted_root;

    const struct vfs_operations *operations;
    void *private_data;
};

struct vfs_filesystem {
    const char *name;
};

int vfs_init(struct vfs_node *root);

int vfs_register_filesystem(const struct vfs_filesystem *filesystem);

int vfs_mount(const char *filesystem_name,
              struct vfs_node *mountpoint,
              struct vfs_node *filesystem_root);

int vfs_add_child(struct vfs_node *directory,
                  struct vfs_node *child);

int vfs_lookup(const char *path, struct vfs_node **result);

int vfs_read(struct vfs_node *node,
             uint64_t offset,
             void *buffer,
             size_t size);

int vfs_write(struct vfs_node *node,
              uint64_t offset,
              const void *buffer,
              size_t size);

int vfs_create(struct vfs_node *directory,
               const char *name,
               enum vfs_node_type type,
               struct vfs_node **result);

int vfs_remove(struct vfs_node *directory,
               struct vfs_node *node);

int vfs_readdir(struct vfs_node *directory,
                size_t index,
                struct vfs_node **result);

struct vfs_node *vfs_root(void);

#endif
