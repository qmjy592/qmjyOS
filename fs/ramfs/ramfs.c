#include "ramfs.h"

static struct vfs_node root_node;
static struct vfs_node nodes[RAMFS_MAX_NODES];

static char names[RAMFS_MAX_NODES][RAMFS_MAX_NAME];
static unsigned char data[RAMFS_MAX_NODES][RAMFS_MAX_FILE_SIZE];
static unsigned char used[RAMFS_MAX_NODES];

static const struct vfs_filesystem ramfs_descriptor = {
    .name = "ramfs"
};

static int name_equal(const char *a, const char *b)
{
    size_t i = 0;

    if (!a || !b)
        return 0;

    while (a[i] && b[i]) {
        if (a[i] != b[i])
            return 0;
        ++i;
    }

    return a[i] == b[i];
}

static int name_valid(const char *name)
{
    size_t i;

    if (!name || !name[0])
        return 0;

    if (name_equal(name, ".") || name_equal(name, ".."))
        return 0;

    for (i = 0; name[i]; ++i) {
        if (name[i] == '/' || i >= RAMFS_MAX_NAME - 1)
            return 0;
    }

    return 1;
}

static int find_slot(void)
{
    size_t i;

    for (i = 0; i < RAMFS_MAX_NODES; ++i) {
        if (!used[i])
            return (int)i;
    }

    return -1;
}

static int node_slot(struct vfs_node *node)
{
    size_t i;

    for (i = 0; i < RAMFS_MAX_NODES; ++i) {
        if (used[i] && &nodes[i] == node)
            return (int)i;
    }

    return -1;
}

static int ramfs_lookup(
    struct vfs_node *directory,
    const char *name,
    struct vfs_node **result)
{
    struct vfs_node *node;

    if (!directory || !name || !result)
        return VFS_ERR_INVALID;

    if (directory->type != VFS_NODE_DIRECTORY)
        return VFS_ERR_NOT_DIRECTORY;

    for (node = directory->children; node; node = node->next) {
        if (name_equal(node->name, name)) {
            *result = node;
            return VFS_OK;
        }
    }

    *result = NULL;
    return VFS_ERR_NOT_FOUND;
}

static int ramfs_read(
    struct vfs_node *node,
    uint64_t offset,
    void *buffer,
    size_t size)
{
    int slot;
    size_t available;
    size_t i;

    if (!node)
        return VFS_ERR_INVALID;

    if (node->type != VFS_NODE_FILE)
        return VFS_ERR_NOT_FILE;

    if (!buffer && size)
        return VFS_ERR_INVALID;

    slot = node_slot(node);

    if (slot < 0)
        return VFS_ERR_INVALID;

    if (!size || offset >= node->size)
        return 0;

    available = (size_t)(node->size - offset);

    if (size > available)
        size = available;

    for (i = 0; i < size; ++i)
        ((unsigned char *)buffer)[i] = data[slot][(size_t)offset + i];

    return (int)size;
}

static int ramfs_write(
    struct vfs_node *node,
    uint64_t offset,
    const void *buffer,
    size_t size)
{
    int slot;
    size_t i;

    if (!node)
        return VFS_ERR_INVALID;

    if (node->type != VFS_NODE_FILE)
        return VFS_ERR_NOT_FILE;

    if (!buffer && size)
        return VFS_ERR_INVALID;

    slot = node_slot(node);

    if (slot < 0)
        return VFS_ERR_INVALID;

    if (offset > RAMFS_MAX_FILE_SIZE)
        return VFS_ERR_NO_SPACE;

    if (size > RAMFS_MAX_FILE_SIZE - (size_t)offset)
        return VFS_ERR_NO_SPACE;

    if (!size)
        return 0;

    /* 稀疏写入区域自动清零。 */
    if (offset > node->size) {
        for (i = (size_t)node->size; i < (size_t)offset; ++i)
            data[slot][i] = 0;
    }

    for (i = 0; i < size; ++i)
        data[slot][(size_t)offset + i] =
            ((const unsigned char *)buffer)[i];

    if (offset + size > node->size)
        node->size = offset + size;

    return (int)size;
}

static int ramfs_create(
    struct vfs_node *directory,
    const char *name,
    enum vfs_node_type type,
    struct vfs_node **result);

static int ramfs_remove(
    struct vfs_node *directory,
    struct vfs_node *node);

static const struct vfs_operations directory_ops = {
    .lookup = ramfs_lookup,
    .create = ramfs_create,
    .remove = ramfs_remove
};

static const struct vfs_operations file_ops = {
    .read = ramfs_read,
    .write = ramfs_write
};

static int ramfs_create(
    struct vfs_node *directory,
    const char *name,
    enum vfs_node_type type,
    struct vfs_node **result)
{
    struct vfs_node *existing;
    struct vfs_node *node;
    int slot;
    size_t i;
    size_t length = 0;

    if (!directory || !result || !name_valid(name))
        return VFS_ERR_INVALID;

    if (directory->type != VFS_NODE_DIRECTORY)
        return VFS_ERR_NOT_DIRECTORY;

    if (type != VFS_NODE_FILE && type != VFS_NODE_DIRECTORY)
        return VFS_ERR_NOT_SUPPORTED;

    if (ramfs_lookup(directory, name, &existing) == VFS_OK)
        return VFS_ERR_EXISTS;

    slot = find_slot();

    if (slot < 0)
        return VFS_ERR_NO_SPACE;

    while (name[length])
        ++length;

    node = &nodes[slot];
    used[slot] = 1;

    for (i = 0; i < RAMFS_MAX_NAME; ++i)
        names[slot][i] = '\0';

    for (i = 0; i < length; ++i)
        names[slot][i] = name[i];

    for (i = 0; i < RAMFS_MAX_FILE_SIZE; ++i)
        data[slot][i] = 0;

    node->name = names[slot];
    node->type = type;
    node->mode = type == VFS_NODE_DIRECTORY ? 0755U : 0644U;
    node->size = 0;
    node->parent = directory;
    node->children = NULL;
    node->next = NULL;
    node->mounted_root = NULL;
    node->operations = type == VFS_NODE_DIRECTORY
                     ? &directory_ops : &file_ops;
    node->private_data = NULL;

    if (!directory->children) {
        directory->children = node;
    } else {
        struct vfs_node *tail = directory->children;

        while (tail->next)
            tail = tail->next;

        tail->next = node;
    }

    *result = node;
    return VFS_OK;
}

static int ramfs_remove(
    struct vfs_node *directory,
    struct vfs_node *node)
{
    struct vfs_node **link;
    int slot;
    size_t i;

    if (!directory || !node)
        return VFS_ERR_INVALID;

    if (directory->type != VFS_NODE_DIRECTORY)
        return VFS_ERR_NOT_DIRECTORY;

    if (node->parent != directory)
        return VFS_ERR_NOT_FOUND;

    if (node->mounted_root)
        return VFS_ERR_BUSY;

    if (node->type == VFS_NODE_DIRECTORY && node->children)
        return VFS_ERR_BUSY;

    slot = node_slot(node);

    if (slot < 0)
        return VFS_ERR_INVALID;

    link = &directory->children;

    while (*link && *link != node)
        link = &(*link)->next;

    if (!*link)
        return VFS_ERR_NOT_FOUND;

    *link = node->next;

    used[slot] = 0;

    for (i = 0; i < RAMFS_MAX_NAME; ++i)
        names[slot][i] = '\0';

    for (i = 0; i < RAMFS_MAX_FILE_SIZE; ++i)
        data[slot][i] = 0;

    node->name = NULL;
    node->type = VFS_NODE_UNKNOWN;
    node->mode = 0;
    node->size = 0;
    node->parent = NULL;
    node->children = NULL;
    node->next = NULL;
    node->mounted_root = NULL;
    node->operations = NULL;
    node->private_data = NULL;

    return VFS_OK;
}

int ramfs_init(void)
{
    size_t i;
    size_t j;

    for (i = 0; i < RAMFS_MAX_NODES; ++i) {
        used[i] = 0;

        for (j = 0; j < RAMFS_MAX_NAME; ++j)
            names[i][j] = '\0';

        for (j = 0; j < RAMFS_MAX_FILE_SIZE; ++j)
            data[i][j] = 0;

        nodes[i].name = NULL;
        nodes[i].type = VFS_NODE_UNKNOWN;
        nodes[i].mode = 0;
        nodes[i].size = 0;
        nodes[i].parent = NULL;
        nodes[i].children = NULL;
        nodes[i].next = NULL;
        nodes[i].mounted_root = NULL;
        nodes[i].operations = NULL;
        nodes[i].private_data = NULL;
    }

    root_node.name = "";
    root_node.type = VFS_NODE_DIRECTORY;
    root_node.mode = 0755U;
    root_node.size = 0;
    root_node.parent = &root_node;
    root_node.children = NULL;
    root_node.next = NULL;
    root_node.mounted_root = NULL;
    root_node.operations = &directory_ops;
    root_node.private_data = NULL;

    return VFS_OK;
}

struct vfs_node *ramfs_root(void)
{
    return &root_node;
}

const struct vfs_filesystem *ramfs_filesystem(void)
{
    return &ramfs_descriptor;
}
