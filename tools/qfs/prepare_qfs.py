from pathlib import Path

def replace_once(path, old, new):
    p = Path(path)
    s = p.read_text()
    if new in s:
        return
    if old not in s:
        raise SystemExit(f"无法定位修改位置：{path}")
    p.write_text(s.replace(old, new, 1))

# 不保留未使用的分区容量变量。
p = Path("fs/qfs/qfs.c")
s = p.read_text()
s = s.replace("static qm_u64_t qfs_partition_sectors;\n", "")
s = s.replace("    qfs_partition_sectors = partition->sector_count;\n", "")
p.write_text(s)

# 稀疏写入时必须确保从第 0 块到最后一个目标块均已分配。
replace_once(
    "fs/qfs/qfs.c",
    """        qm_u32_t first = (qm_u32_t)offset / QFS_SECTOR_SIZE;
        qm_u32_t last = (end - 1U) / QFS_SECTOR_SIZE;""",
    """        qm_u32_t first = 0;
        qm_u32_t last = (end - 1U) / QFS_SECTOR_SIZE;"""
)

# 不允许格式化一个已经挂载的文件系统。
replace_once(
    "fs/qfs/qfs.c",
    """int qfs_format(const qm_partition_t *partition)
{
    qm_u8_t sector[QFS_SECTOR_SIZE];
    qm_u32_t i;

    if (!qfs_valid_partition(partition))""",
    """int qfs_format(const qm_partition_t *partition)
{
    qm_u8_t sector[QFS_SECTOR_SIZE];
    qm_u32_t i;

    if (qfs_is_mounted)
        return VFS_ERR_BUSY;

    if (!qfs_valid_partition(partition))"""
)

# 验证 inode 中的数据块指针，不能指向超级块或元数据。
replace_once(
    "fs/qfs/qfs.c",
    """    if (qfs_inodes[index].used &&
        (qfs_inodes[index].type != QFS_INODE_FILE ||
         qfs_inodes[index].size > QFS_MAX_FILE_SIZE))
        return QFS_ERR_CORRUPT;

    return 0;""",
    """    if (qfs_inodes[index].used) {
        if (qfs_inodes[index].type != QFS_INODE_FILE ||
            qfs_inodes[index].size > QFS_MAX_FILE_SIZE)
            return QFS_ERR_CORRUPT;

        for (i = 0; i < QFS_DIRECT_BLOCKS; ++i) {
            qm_u32_t block = qfs_inodes[index].blocks[i];

            if (block != 0 &&
                (block < QFS_DATA_START ||
                 block >= qfs_total_sectors ||
                 !qfs_bitmap_test(block)))
                return QFS_ERR_CORRUPT;
        }
    }

    return 0;"""
)

# 挂载时检查位图中的元数据保留区。
replace_once(
    "fs/qfs/qfs.c",
    """    for (i = 0; i < QFS_MAX_INODES; ++i) {
        if (qfs_load_inode(i) != 0) {""",
    """    for (i = 0; i < QFS_DATA_START; ++i) {
        if (!qfs_bitmap_test(i)) {
            qfs_is_mounted = 0;
            return VFS_ERR_INVALID;
        }
    }

    for (i = 0; i < QFS_MAX_INODES; ++i) {
        if (qfs_load_inode(i) != 0) {"""
)

print("QFS safety patches applied.")
