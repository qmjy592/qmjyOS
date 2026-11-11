#include <stdio.h>
#include <string.h>

#include "fs/vfs/vfs.h"
#include "fs/ramfs/ramfs.h"
#include "fs/core/fs_init.h"

#define CHECK(condition, message)                 \
    do {                                           \
        if (!(condition)) {                        \
            printf("[FAIL] %s\n", message);       \
            return 1;                              \
        }                                          \
        printf("[PASS] %s\n", message);           \
    } while (0)

int main(void)
{
    struct vfs_node *root;
    struct vfs_node *directory = NULL;
    struct vfs_node *file = NULL;
    struct vfs_node *found = NULL;

    char buffer[64];
    const char content[] = "qmjyOS RAMFS test";
    size_t i;
    int zero_filled = 1;

    CHECK(fs_init() == VFS_OK, "initialize VFS and RAMFS");

    root = vfs_root();

    CHECK(root == ramfs_root(), "RAMFS is VFS root");

    CHECK(vfs_create(root, "docs",
                     VFS_NODE_DIRECTORY, &directory) == VFS_OK,
          "create directory");

    CHECK(vfs_create(root, "docs",
                     VFS_NODE_DIRECTORY, &found) == VFS_ERR_EXISTS,
          "reject duplicate directory");

    CHECK(vfs_create(directory, "readme.txt",
                     VFS_NODE_FILE, &file) == VFS_OK,
          "create file");

    CHECK(vfs_lookup("/docs/readme.txt", &found) == VFS_OK &&
          found == file,
          "lookup file by absolute path");

    CHECK(vfs_write(file, 0, content, sizeof(content)) ==
          (int)sizeof(content),
          "write file");

    memset(buffer, 0, sizeof(buffer));

    CHECK(vfs_read(file, 0, buffer, sizeof(buffer)) ==
          (int)sizeof(content) &&
          memcmp(buffer, content, sizeof(content)) == 0,
          "read back file contents");

    CHECK(vfs_write(file, 32, "X", 1) == 1,
          "sparse write");

    memset(buffer, 0xAA, sizeof(buffer));

    CHECK(vfs_read(file, sizeof(content), buffer,
                   33 - sizeof(content)) ==
          (int)(33 - sizeof(content)),
          "read sparse region");

    for (i = sizeof(content); i < 32; ++i) {
        if (buffer[i - sizeof(content)] != 0)
            zero_filled = 0;
    }

    if (buffer[32 - sizeof(content)] != 'X')
        zero_filled = 0;

    CHECK(zero_filled, "sparse gap is zero-filled");

    CHECK(vfs_read(file, 4096, buffer, 1) == 0,
          "read beyond EOF");

    CHECK(vfs_write(file, 4096, "X", 1) == VFS_ERR_NO_SPACE,
          "reject write beyond capacity");

    CHECK(vfs_remove(root, directory) == VFS_ERR_BUSY,
          "reject removal of non-empty directory");

    CHECK(vfs_remove(directory, file) == VFS_OK,
          "remove file");

    CHECK(vfs_lookup("/docs/readme.txt", &found) ==
          VFS_ERR_NOT_FOUND,
          "removed file is no longer visible");

    CHECK(vfs_remove(root, directory) == VFS_OK,
          "remove empty directory");

    CHECK(vfs_lookup("/docs", &found) == VFS_ERR_NOT_FOUND,
          "removed directory is no longer visible");

    printf("\nRAMFS TESTS PASSED\n");
    return 0;
}
