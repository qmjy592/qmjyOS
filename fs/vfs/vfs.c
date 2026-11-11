#include "vfs.h"

static struct vfs_node *vfs_root_node;

static const struct vfs_filesystem *registered_filesystems[VFS_MAX_FILESYSTEMS];
static size_t registered_filesystem_count;

static int string_equal(const char *left, const char *right)
{
    size_t i = 0;

    if (left == NULL || right == NULL)
        return 0;

    while (left[i] != '\0' && right[i] != '\0') {
        if (left[i] != right[i])
            return 0;
        ++i;
    }

    return left[i] == right[i];
}

static int valid_name(const char *name)
{
    size_t i;

    if (name == NULL || name[0] == '\0')
        return 0;

    if (string_equal(name, ".") || string_equal(name, ".."))
        return 0;

    for (i = 0; name[i] != '\0'; ++i) {
        if (name[i] == '/')
            return 0;
    }

    return 1;
}

static struct vfs_filesystem *find_filesystem(const char *name)
{
    size_t i;

    for (i = 0; i < registered_filesystem_count; ++i) {
        if (string_equal(registered_filesystems[i]->name, name))
            return (struct vfs_filesystem *)registered_filesystems[i];
    }

    return NULL;
}

static int follow_mounts(struct vfs_node **node)
{
    size_t depth = 0;

    while (*node != NULL && (*node)->mounted_root != NULL) {
        if (depth++ >= VFS_MAX_MOUNT_DEPTH)
            return VFS_ERR_BUSY;

        *node = (*node)->mounted_root;
    }

    return VFS_OK;
}

static int find_child(struct vfs_node *directory,
                      const char *name,
                      struct vfs_node **result)
{
    struct vfs_node *child;

    for (child = directory->children; child != NULL; child = child->next) {
        if (string_equal(child->name, name)) {
            *result = child;
            return VFS_OK;
        }
    }

    if (directory->operations != NULL &&
        directory->operations->lookup != NULL) {
        return directory->operations->lookup(directory, name, result);
    }

    return VFS_ERR_NOT_FOUND;
}

int vfs_init(struct vfs_node *root)
{
    if (root == NULL || root->type != VFS_NODE_DIRECTORY)
        return VFS_ERR_INVALID;

    root->parent = root;
    vfs_root_node = root;
    registered_filesystem_count = 0;

    return VFS_OK;
}

struct vfs_node *vfs_root(void)
{
    return vfs_root_node;
}

int vfs_register_filesystem(const struct vfs_filesystem *filesystem)
{
    if (filesystem == NULL || filesystem->name == NULL ||
        filesystem->name[0] == '\0')
        return VFS_ERR_INVALID;

    if (find_filesystem(filesystem->name) != NULL)
        return VFS_ERR_EXISTS;

    if (registered_filesystem_count >= VFS_MAX_FILESYSTEMS)
        return VFS_ERR_NO_SPACE;

    registered_filesystems[registered_filesystem_count++] = filesystem;
    return VFS_OK;
}

int vfs_add_child(struct vfs_node *directory,
                  struct vfs_node *child)
{
    struct vfs_node *current;

    if (directory == NULL || child == NULL ||
        directory->type != VFS_NODE_DIRECTORY ||
        !valid_name(child->name) ||
        child->parent != NULL || child->next != NULL)
        return VFS_ERR_INVALID;

    for (current = directory->children;
         current != NULL;
         current = current->next) {
        if (string_equal(current->name, child->name))
            return VFS_ERR_EXISTS;
    }

    child->parent = directory;

    if (directory->children == NULL) {
        directory->children = child;
    } else {
        current = directory->children;
        while (current->next != NULL)
            current = current->next;
        current->next = child;
    }

    return VFS_OK;
}

int vfs_mount(const char *filesystem_name,
              struct vfs_node *mountpoint,
              struct vfs_node *filesystem_root)
{
    if (filesystem_name == NULL || mountpoint == NULL ||
        filesystem_root == NULL)
        return VFS_ERR_INVALID;

    if (find_filesystem(filesystem_name) == NULL)
        return VFS_ERR_NOT_FOUND;

    if (mountpoint->type != VFS_NODE_DIRECTORY ||
        filesystem_root->type != VFS_NODE_DIRECTORY)
        return VFS_ERR_NOT_DIRECTORY;

    if (mountpoint->mounted_root != NULL ||
        filesystem_root == vfs_root_node)
        return VFS_ERR_BUSY;

    mountpoint->mounted_root = filesystem_root;
    filesystem_root->parent = mountpoint;

    return VFS_OK;
}

int vfs_lookup(const char *path, struct vfs_node **result)
{
    struct vfs_node *current;
    size_t position = 0;

    if (path == NULL || result == NULL ||
        path[0] != '/' || vfs_root_node == NULL)
        return VFS_ERR_INVALID;

    *result = NULL;
    current = vfs_root_node;

    while (path[position] != '\0') {
        size_t start;
        size_t length;
        size_t i;
        char component[256];

        while (path[position] == '/')
            ++position;

        if (path[position] == '\0')
            break;

        start = position;

        while (path[position] != '\0' && path[position] != '/')
            ++position;

        length = position - start;

        if (length >= sizeof(component))
            return VFS_ERR_INVALID;

        for (i = 0; i < length; ++i)
            component[i] = path[start + i];

        component[length] = '\0';

        if (string_equal(component, ".")) {
            continue;
        }

        if (follow_mounts(&current) != VFS_OK)
            return VFS_ERR_BUSY;

        if (string_equal(component, "..")) {
            if (current->parent != NULL)
                current = current->parent;
            continue;
        }

        if (current->type != VFS_NODE_DIRECTORY)
            return VFS_ERR_NOT_DIRECTORY;

        {
            struct vfs_node *next = NULL;
            int status = find_child(current, component, &next);

            if (status != VFS_OK)
                return status;

            if (next == NULL)
                return VFS_ERR_NOT_FOUND;

            current = next;
        }
    }

    if (follow_mounts(&current) != VFS_OK)
        return VFS_ERR_BUSY;

    *result = current;
    return VFS_OK;
}

int vfs_read(struct vfs_node *node,
             uint64_t offset,
             void *buffer,
             size_t size)
{
    if (node == NULL || (buffer == NULL && size != 0))
        return VFS_ERR_INVALID;

    if (node->type != VFS_NODE_FILE && node->type != VFS_NODE_DEVICE)
        return VFS_ERR_NOT_FILE;

    if (node->operations == NULL || node->operations->read == NULL)
        return VFS_ERR_NOT_SUPPORTED;

    return node->operations->read(node, offset, buffer, size);
}

int vfs_write(struct vfs_node *node,
              uint64_t offset,
              const void *buffer,
              size_t size)
{
    if (node == NULL || (buffer == NULL && size != 0))
        return VFS_ERR_INVALID;

    if (node->type != VFS_NODE_FILE && node->type != VFS_NODE_DEVICE)
        return VFS_ERR_NOT_FILE;

    if (node->operations == NULL || node->operations->write == NULL)
        return VFS_ERR_NOT_SUPPORTED;

    return node->operations->write(node, offset, buffer, size);
}

int vfs_create(struct vfs_node *directory,
               const char *name,
               enum vfs_node_type type,
               struct vfs_node **result)
{
    if (directory == NULL || !valid_name(name) || result == NULL)
        return VFS_ERR_INVALID;

    if (directory->type != VFS_NODE_DIRECTORY)
        return VFS_ERR_NOT_DIRECTORY;

    if (directory->operations == NULL ||
        directory->operations->create == NULL)
        return VFS_ERR_NOT_SUPPORTED;

    return directory->operations->create(directory, name, type, result);
}

int vfs_remove(struct vfs_node *directory, struct vfs_node *node)
{
    if (directory == NULL || node == NULL)
        return VFS_ERR_INVALID;

    if (directory->type != VFS_NODE_DIRECTORY)
        return VFS_ERR_NOT_DIRECTORY;

    if (directory->operations == NULL ||
        directory->operations->remove == NULL)
        return VFS_ERR_NOT_SUPPORTED;

    return directory->operations->remove(directory, node);
}

int vfs_readdir(struct vfs_node *directory,
                size_t index,
                struct vfs_node **result)
{
    struct vfs_node *child;
    size_t current_index = 0;

    if (directory == NULL || result == NULL)
        return VFS_ERR_INVALID;

    *result = NULL;

    if (follow_mounts(&directory) != VFS_OK)
        return VFS_ERR_BUSY;

    if (directory->type != VFS_NODE_DIRECTORY)
        return VFS_ERR_NOT_DIRECTORY;

    for (child = directory->children;
         child != NULL;
         child = child->next) {
        if (current_index++ == index) {
            *result = child;
            return VFS_OK;
        }
    }

    return VFS_ERR_NOT_FOUND;
}
