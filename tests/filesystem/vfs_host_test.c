#include <stdio.h>
#include <string.h>

#include "fs/vfs/vfs.h"

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        printf("FAIL: %s\n", message); \
        return 1; \
    } \
    printf("PASS: %s\n", message); \
} while (0)

static char file_data[64];

static int test_read(struct vfs_node *node,
                     uint64_t offset,
                     void *buffer,
                     size_t size)
{
    size_t available;

    (void)node;

    if (offset >= sizeof(file_data))
        return 0;

    available = sizeof(file_data) - (size_t)offset;
    if (size > available)
        size = available;

    memcpy(buffer, file_data + offset, size);
    return (int)size;
}

static int test_write(struct vfs_node *node,
                      uint64_t offset,
                      const void *buffer,
                      size_t size)
{
    (void)node;

    if (offset > sizeof(file_data) ||
        size > sizeof(file_data) - (size_t)offset)
        return VFS_ERR_IO;

    memcpy(file_data + offset, buffer, size);
    return (int)size;
}

static const struct vfs_operations file_operations = {
    .read = test_read,
    .write = test_write
};

int main(void)
{
    static struct vfs_node root = {
        .name = "",
        .type = VFS_NODE_DIRECTORY
    };

    static struct vfs_node home = {
        .name = "home",
        .type = VFS_NODE_DIRECTORY
    };

    static struct vfs_node user = {
        .name = "user",
        .type = VFS_NODE_DIRECTORY
    };

    static struct vfs_node hello = {
        .name = "hello.txt",
        .type = VFS_NODE_FILE,
        .operations = &file_operations
    };

    static struct vfs_node mnt = {
        .name = "mnt",
        .type = VFS_NODE_DIRECTORY
    };

    static struct vfs_node ram_root = {
        .name = "",
        .type = VFS_NODE_DIRECTORY
    };

    static struct vfs_node ram_file = {
        .name = "ram.txt",
        .type = VFS_NODE_FILE,
        .operations = &file_operations
    };

    static const struct vfs_filesystem ramfs = {
        .name = "ramfs"
    };

    struct vfs_node *found = NULL;
    struct vfs_node *entry = NULL;
    char buffer[16] = {0};
    const char message[] = "qmjyOS";

    CHECK(vfs_init(&root) == VFS_OK,
          "VFS initializes with a directory root");

    CHECK(vfs_add_child(&root, &home) == VFS_OK,
          "root accepts a child directory");

    CHECK(vfs_add_child(&home, &user) == VFS_OK,
          "directory tree accepts nested directories");

    CHECK(vfs_add_child(&user, &hello) == VFS_OK,
          "directory accepts a file");

    CHECK(vfs_add_child(&root, &mnt) == VFS_OK,
          "mount point directory is created");

    CHECK(vfs_lookup("/home/user/hello.txt", &found) == VFS_OK &&
          found == &hello,
          "absolute path lookup resolves nested files");

    CHECK(vfs_lookup("//home///user/./hello.txt", &found) == VFS_OK &&
          found == &hello,
          "path lookup handles repeated separators and dot");

    CHECK(vfs_lookup("/home/user/../user", &found) == VFS_OK &&
          found == &user,
          "path lookup handles parent directory");

    CHECK(vfs_lookup("/missing", &found) == VFS_ERR_NOT_FOUND,
          "missing path returns not-found");

    CHECK(vfs_lookup("relative/path", &found) == VFS_ERR_INVALID,
          "relative path is rejected");

    CHECK(vfs_write(&hello, 0, message, sizeof(message)) ==
          (int)sizeof(message),
          "VFS dispatches file writes");

    CHECK(vfs_read(&hello, 0, buffer, sizeof(message)) ==
          (int)sizeof(message) &&
          memcmp(buffer, message, sizeof(message)) == 0,
          "VFS dispatches file reads");

    CHECK(vfs_readdir(&root, 0, &entry) == VFS_OK &&
          entry == &home,
          "directory enumeration returns first child");

    CHECK(vfs_readdir(&root, 99, &entry) == VFS_ERR_NOT_FOUND,
          "directory enumeration detects end of directory");

    CHECK(vfs_register_filesystem(&ramfs) == VFS_OK,
          "filesystem registration succeeds");

    CHECK(vfs_register_filesystem(&ramfs) == VFS_ERR_EXISTS,
          "duplicate filesystem registration is rejected");

    CHECK(vfs_add_child(&ram_root, &ram_file) == VFS_OK,
          "RAM filesystem root accepts a file");

    CHECK(vfs_mount("ramfs", &mnt, &ram_root) == VFS_OK,
          "registered filesystem mounts successfully");

    CHECK(vfs_lookup("/mnt/ram.txt", &found) == VFS_OK &&
          found == &ram_file,
          "path lookup crosses a mount point");

    CHECK(vfs_mount("unknown", &home, &ram_root) == VFS_ERR_NOT_FOUND,
          "unregistered filesystem cannot be mounted");

    CHECK(vfs_mount("ramfs", &mnt, &ram_root) == VFS_ERR_BUSY,
          "mount point cannot be mounted twice");

    CHECK(vfs_read(&home, 0, buffer, 1) == VFS_ERR_NOT_FILE,
          "reading a directory is rejected");

    CHECK(vfs_create(&home, "new.txt", VFS_NODE_FILE, &found) ==
          VFS_ERR_NOT_SUPPORTED,
          "unsupported create operation returns explicit error");

    printf("VFS HOST TESTS PASSED\n");
    return 0;
}
