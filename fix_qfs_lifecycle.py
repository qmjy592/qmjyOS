from pathlib import Path
import sys

path = Path("fs/qfs/qfs.c")

if not path.is_file():
    sys.exit("[ERROR] fs/qfs/qfs.c not found")

source = path.read_text()

def replace_once(text, old, new, description):
    count = text.count(old)
    if count != 1:
        raise RuntimeError(
            f"{description}: expected 1 match, found {count}"
        )
    return text.replace(old, new, 1)

try:
    # 1. Filename validation helper.
    if "static int qfs_valid_name(" not in source:
        anchor = r"""static void qfs_zero(void *buffer, qm_u32_t size)
{"""

        helper = r"""static int qfs_valid_name(const char *name)
{
    qm_u32_t i;
    qm_u32_t length;

    if (name == 0 || name[0] == '\0')
        return 0;

    if (qfs_string_equal(name, ".") ||
        qfs_string_equal(name, ".."))
        return 0;

    length = qfs_string_length(name);

    if (length == 0 || length >= QFS_MAX_NAME)
        return 0;

    for (i = 0; i < length; ++i) {
        if (name[i] == '/')
            return 0;
    }

    return 1;
}

static void qfs_zero(void *buffer, qm_u32_t size)
{"""

        source = replace_once(
            source, anchor, helper,
            "filename helper insertion"
        )

    # 2. qfs_create validation.
    old = r"""    length = qfs_string_length(name);

    if (length == 0 || length >= QFS_MAX_NAME)
        return VFS_ERR_INVALID;"""

    new = r"""    if (!qfs_valid_name(name))
        return VFS_ERR_INVALID;

    length = qfs_string_length(name);"""

    if old in source:
        source = replace_once(
            source, old, new,
            "qfs_create filename validation"
        )
    elif "if (!qfs_valid_name(name))" not in source:
        raise RuntimeError(
            "Cannot locate qfs_create filename validation"
        )

    # 3. On-disk directory filename validation.
    old = r"""                qfs_directory[i].name[0] == '\0') {"""

    new = r"""                !qfs_valid_name(qfs_directory[i].name)) {"""

    if old in source:
        source = replace_once(
            source, old, new,
            "mount directory filename validation"
        )
    elif "!qfs_valid_name(qfs_directory[i].name)" not in source:
        raise RuntimeError(
            "Cannot locate mount filename validation"
        )

    # 4. Reset state before direct mount.
    old = r"""    qfs_device_id = partition->device_id;
    qfs_partition_start = partition->start_lba;
    qfs_total_sectors = (qm_u32_t)partition->sector_count;

    /*
     * Enable bounded reads while validating the superblock.
     */"""

    new = r"""    qfs_device_id = partition->device_id;
    qfs_partition_start = partition->start_lba;
    qfs_total_sectors = (qm_u32_t)partition->sector_count;
    qfs_mountpoint = 0;
    qfs_root_node.parent = &qfs_root_node;
    qfs_root_node.children = 0;

    /*
     * Enable bounded reads while validating the superblock.
     */"""

    source = replace_once(
        source, old, new,
        "qfs_mount state initialization"
    )

    # 5. Replace qfs_mount_at.
    mount_start = source.find("int qfs_mount_at(")
    unmount_start = source.find(
        "\nint qfs_unmount(void)", mount_start
    )

    if mount_start < 0 or unmount_start < 0:
        raise RuntimeError(
            "Cannot locate qfs_mount_at/qfs_unmount"
        )

    new_mount_at = r"""int qfs_mount_at(
    const qm_partition_t *partition,
    const char *target)
{
    struct vfs_node *mountpoint;
    int result;

    if (target == 0)
        return VFS_ERR_INVALID;

    result = qfs_mount(partition);

    if (result != VFS_OK)
        return result;

    result = vfs_lookup(target, &mountpoint);

    if (result != VFS_OK) {
        qfs_is_mounted = 0;
        qfs_mountpoint = 0;
        qfs_root_node.parent = &qfs_root_node;
        qfs_root_node.children = 0;
        return result;
    }

    result = vfs_mount("qfs", mountpoint, qfs_root());

    if (result != VFS_OK) {
        qfs_is_mounted = 0;
        qfs_mountpoint = 0;
        qfs_root_node.parent = &qfs_root_node;
        qfs_root_node.children = 0;
        return result;
    }

    qfs_mountpoint = mountpoint;
    return VFS_OK;
}
"""

    source = (
        source[:mount_start]
        + new_mount_at
        + source[unmount_start:]
    )

    # 6. Replace qfs_unmount.
    unmount_start = source.find("int qfs_unmount(void)")
    root_start = source.find(
        "\nstruct vfs_node *qfs_root(void)",
        unmount_start
    )

    if unmount_start < 0 or root_start < 0:
        raise RuntimeError(
            "Cannot locate qfs_unmount/qfs_root"
        )

    new_unmount = r"""int qfs_unmount(void)
{
    if (!qfs_is_mounted)
        return VFS_ERR_INVALID;

    if (qfs_flush() != 0)
        return VFS_ERR_IO;

    if (qfs_mountpoint != 0) {
        if (qfs_mountpoint->mounted_root != &qfs_root_node)
            return VFS_ERR_BUSY;

        qfs_mountpoint->mounted_root = 0;
    }

    qfs_mountpoint = 0;
    qfs_root_node.parent = &qfs_root_node;
    qfs_root_node.children = 0;
    qfs_is_mounted = 0;

    return VFS_OK;
}
"""

    source = (
        source[:unmount_start]
        + new_unmount
        + source[root_start:]
    )

    # 7. Sanity checks before writing.
    for signature in (
        "static int qfs_valid_name(",
        "int qfs_mount_at(",
        "int qfs_unmount(void)"
    ):
        if source.count(signature) != 1:
            raise RuntimeError(
                f"Invalid definition count: {signature}"
            )

    path.write_text(source)

except Exception as error:
    sys.exit(f"[ERROR] {error}\nNo changes were written.")

print("[OK] Filename validation updated.")
print("[OK] Create validation updated.")
print("[OK] Mount validation updated.")
print("[OK] Mount state initialization updated.")
print("[OK] Mount failure cleanup updated.")
print("[OK] Unmount lifecycle updated.")
print("[OK] Changes written.")
