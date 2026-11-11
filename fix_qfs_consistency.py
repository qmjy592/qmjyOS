from pathlib import Path
import sys

path = Path("fs/qfs/qfs.c")
if not path.is_file():
    sys.exit("[ERROR] fs/qfs/qfs.c not found")

source = path.read_text()

def replace_function(text, start_signature, end_signature, replacement):
    start = text.find(start_signature)
    if start < 0:
        raise RuntimeError("Cannot find function: " + start_signature)

    end = text.find(end_signature, start)
    if end < 0:
        raise RuntimeError("Cannot find next function: " + end_signature)

    return text[:start] + replacement + "\n\n" + text[end:]

try:
    # ------------------------------------------------
    # 1. Replace qfs_write_file.
    # ------------------------------------------------

    write_function = r"""static int qfs_write_file(
    struct vfs_node *node,
    uint64_t offset,
    const void *buffer,
    size_t size)
{
    int inode_index;
    int failure = VFS_ERR_IO;
    const qm_u8_t *source = (const qm_u8_t *)buffer;
    qm_u8_t sector[QFS_SECTOR_SIZE];
    qm_u8_t old_data[QFS_DIRECT_BLOCKS][QFS_SECTOR_SIZE];
    qm_u8_t had_old[QFS_DIRECT_BLOCKS];
    qm_u32_t new_blocks[QFS_DIRECT_BLOCKS];
    qm_u32_t new_count = 0;
    qm_u32_t written = 0;
    qm_u32_t end;
    qm_u32_t last;
    qm_u32_t i;
    struct qfs_inode original_inode;

    if (node == 0 || (buffer == 0 && size != 0))
        return VFS_ERR_INVALID;

    inode_index = qfs_node_inode(node);

    if (inode_index <= 0 ||
        inode_index >= (int)QFS_MAX_INODES ||
        !qfs_inodes[inode_index].used)
        return VFS_ERR_INVALID;

    if (offset > QFS_MAX_FILE_SIZE ||
        size > QFS_MAX_FILE_SIZE - (qm_u32_t)offset)
        return VFS_ERR_NO_SPACE;

    if (size == 0)
        return 0;

    end = (qm_u32_t)offset + (qm_u32_t)size;
    last = (end - 1U) / QFS_SECTOR_SIZE;

    original_inode = qfs_inodes[inode_index];
    qfs_zero(had_old, sizeof(had_old));

    /*
     * Back up every existing block that may be modified.
     * This permits best-effort restoration of old file contents
     * if a later write operation fails.
     */
    for (i = 0; i <= last; ++i) {
        qm_u32_t physical = original_inode.blocks[i];

        if (physical != 0) {
            if (qfs_read_sector(
                    physical,
                    old_data[i]) != 0)
                return VFS_ERR_IO;

            had_old[i] = 1;
        }
    }

    /*
     * Allocate all required blocks before changing file data.
     * Bitmap updates are persisted by qfs_allocate_block().
     */
    for (i = 0; i <= last; ++i) {
        if (qfs_inodes[inode_index].blocks[i] == 0) {
            qm_u32_t allocated;

            failure = qfs_allocate_block(&allocated);

            if (failure != 0) {
                failure = (failure == VFS_ERR_NO_SPACE)
                    ? VFS_ERR_NO_SPACE : VFS_ERR_IO;
                goto rollback;
            }

            new_blocks[new_count++] = allocated;
            qfs_inodes[inode_index].blocks[i] = allocated;

            qfs_zero(sector, sizeof(sector));

            if (qfs_write_sector(allocated, sector) != 0) {
                failure = VFS_ERR_IO;
                goto rollback;
            }
        }
    }

    /*
     * Zero-fill the gap when writing beyond the old EOF.
     */
    if ((qm_u32_t)offset > original_inode.size) {
        qm_u32_t gap = (qm_u32_t)offset - original_inode.size;
        qm_u32_t gap_position = original_inode.size;

        while (gap != 0) {
            qm_u32_t logical =
                gap_position / QFS_SECTOR_SIZE;
            qm_u32_t in_sector =
                gap_position % QFS_SECTOR_SIZE;
            qm_u32_t chunk =
                QFS_SECTOR_SIZE - in_sector;

            if (chunk > gap)
                chunk = gap;

            if (qfs_read_sector(
                    qfs_inodes[inode_index].blocks[logical],
                    sector) != 0) {
                failure = VFS_ERR_IO;
                goto rollback;
            }

            qfs_zero(sector + in_sector, chunk);

            if (qfs_write_sector(
                    qfs_inodes[inode_index].blocks[logical],
                    sector) != 0) {
                failure = VFS_ERR_IO;
                goto rollback;
            }

            gap_position += chunk;
            gap -= chunk;
        }
    }

    /*
     * Write the requested data.
     */
    while (written < size) {
        qm_u32_t position = (qm_u32_t)offset + written;
        qm_u32_t logical = position / QFS_SECTOR_SIZE;
        qm_u32_t in_sector = position % QFS_SECTOR_SIZE;
        qm_u32_t chunk = QFS_SECTOR_SIZE - in_sector;
        qm_u32_t physical =
            qfs_inodes[inode_index].blocks[logical];

        if (chunk > (qm_u32_t)size - written)
            chunk = (qm_u32_t)size - written;

        if (in_sector != 0 || chunk != QFS_SECTOR_SIZE) {
            if (qfs_read_sector(physical, sector) != 0) {
                failure = VFS_ERR_IO;
                goto rollback;
            }
        } else {
            qfs_zero(sector, sizeof(sector));
        }

        qfs_copy(
            sector + in_sector,
            source + written,
            chunk);

        if (qfs_write_sector(physical, sector) != 0) {
            failure = VFS_ERR_IO;
            goto rollback;
        }

        written += chunk;
    }

    if (end > qfs_inodes[inode_index].size)
        qfs_inodes[inode_index].size = end;

    if (qfs_save_inode((qm_u32_t)inode_index) != 0) {
        failure = VFS_ERR_IO;
        goto rollback;
    }

    if (qfs_sync_node((qm_u32_t)inode_index) != 0) {
        failure = VFS_ERR_IO;
        goto rollback;
    }

    if (qfs_flush() != 0) {
        failure = VFS_ERR_IO;
        goto rollback;
    }

    return (int)written;

rollback:
    /*
     * Restore old data first. Newly allocated blocks do not need
     * restoration because they will be returned to the bitmap.
     */
    for (i = 0; i <= last; ++i) {
        if (had_old[i]) {
            if (qfs_write_sector(
                    original_inode.blocks[i],
                    old_data[i]) != 0)
                failure = VFS_ERR_IO;
        }
    }

    /*
     * Restore the original inode before freeing new blocks.
     * Never free a block if the inode rollback cannot be persisted.
     */
    qfs_inodes[inode_index] = original_inode;

    if (qfs_save_inode((qm_u32_t)inode_index) != 0) {
        node->size = original_inode.size;
        qfs_flush();
        return VFS_ERR_IO;
    }

    node->size = original_inode.size;

    for (i = 0; i < new_count; ++i) {
        if (qfs_free_block(new_blocks[i]) != 0)
            failure = VFS_ERR_IO;
    }

    if (qfs_flush() != 0)
        failure = VFS_ERR_IO;

    return failure;
}"""

    source = replace_function(
        source,
        "static int qfs_write_file(",
        "static int qfs_lookup(",
        write_function
    )

    # ------------------------------------------------
    # 2. Replace qfs_create.
    # ------------------------------------------------

    create_function = r"""static int qfs_create(
    struct vfs_node *directory,
    const char *name,
    enum vfs_node_type type,
    struct vfs_node **result)
{
    qm_u32_t length;
    int inode_index;
    int directory_index;
    qm_u32_t i;
    int rollback_failed = 0;

    if (directory != &qfs_root_node ||
        name == 0 || result == 0)
        return VFS_ERR_INVALID;

    *result = 0;

    if (type != VFS_NODE_FILE)
        return VFS_ERR_NOT_SUPPORTED;

    if (!qfs_valid_name(name))
        return VFS_ERR_INVALID;

    length = qfs_string_length(name);

    if (qfs_find_inode(name) >= 0)
        return VFS_ERR_EXISTS;

    inode_index = qfs_find_free_inode();
    directory_index = qfs_find_free_directory();

    if (inode_index < 0 || directory_index < 0)
        return VFS_ERR_NO_SPACE;

    qfs_zero(
        &qfs_inodes[inode_index],
        sizeof(qfs_inodes[inode_index]));

    qfs_inodes[inode_index].used = 1;
    qfs_inodes[inode_index].type = QFS_INODE_FILE;

    qfs_zero(qfs_names[inode_index], QFS_MAX_NAME);

    for (i = 0; i < length; ++i)
        qfs_names[inode_index][i] = name[i];

    qfs_directory[directory_index].inode =
        (qm_u32_t)inode_index;

    qfs_zero(
        qfs_directory[directory_index].name,
        QFS_MAX_NAME);

    for (i = 0; i < length; ++i)
        qfs_directory[directory_index].name[i] = name[i];

    /*
     * Persist inode first, then its directory reference.
     * If either operation fails, attempt to restore both records.
     */
    if (qfs_save_inode((qm_u32_t)inode_index) != 0)
        goto rollback;

    if (qfs_save_directory() != 0)
        goto rollback;

    if (qfs_flush() != 0)
        goto rollback;

    qfs_zero(
        &qfs_nodes[inode_index],
        sizeof(qfs_nodes[inode_index]));

    qfs_nodes[inode_index].name = qfs_names[inode_index];
    qfs_nodes[inode_index].type = VFS_NODE_FILE;
    qfs_nodes[inode_index].mode = 0644U;
    qfs_nodes[inode_index].size = 0;
    qfs_nodes[inode_index].parent = &qfs_root_node;
    qfs_nodes[inode_index].operations = &qfs_file_operations;

    if (qfs_root_node.children == 0) {
        qfs_root_node.children = &qfs_nodes[inode_index];
    } else {
        struct vfs_node *tail = qfs_root_node.children;

        while (tail->next != 0)
            tail = tail->next;

        tail->next = &qfs_nodes[inode_index];
    }

    *result = &qfs_nodes[inode_index];
    return VFS_OK;

rollback:
    qfs_zero(
        &qfs_directory[directory_index],
        sizeof(qfs_directory[directory_index]));

    qfs_zero(
        &qfs_inodes[inode_index],
        sizeof(qfs_inodes[inode_index]));

    if (qfs_save_inode((qm_u32_t)inode_index) != 0)
        rollback_failed = 1;

    if (qfs_save_directory() != 0)
        rollback_failed = 1;

    if (qfs_flush() != 0)
        rollback_failed = 1;

    qfs_zero(qfs_names[inode_index], QFS_MAX_NAME);
    qfs_zero(&qfs_nodes[inode_index], sizeof(qfs_nodes[inode_index]));

    return rollback_failed ? VFS_ERR_IO : VFS_ERR_IO;
}"""

    source = replace_function(
        source,
        "static int qfs_create(",
        "static int qfs_remove(",
        create_function
    )

    # ------------------------------------------------
    # 3. Replace qfs_remove.
    # ------------------------------------------------

    remove_function = r"""static int qfs_remove(
    struct vfs_node *directory,
    struct vfs_node *node)
{
    struct vfs_node **link;
    int inode_index;
    int directory_index = -1;
    qm_u32_t i;
    struct qfs_inode saved_inode;
    struct qfs_directory_entry saved_entry;
    int rollback_failed = 0;
    int reclaim_failed = 0;

    if (directory != &qfs_root_node || node == 0)
        return VFS_ERR_INVALID;

    if (node->parent != directory)
        return VFS_ERR_NOT_FOUND;

    inode_index = qfs_node_inode(node);

    if (inode_index <= 0 ||
        inode_index >= (int)QFS_MAX_INODES ||
        !qfs_inodes[inode_index].used)
        return VFS_ERR_INVALID;

    link = &qfs_root_node.children;

    while (*link != 0 && *link != node)
        link = &(*link)->next;

    if (*link == 0)
        return VFS_ERR_NOT_FOUND;

    for (i = 0; i < QFS_DIRECTORY_ENTRIES; ++i) {
        if (qfs_directory[i].inode == (qm_u32_t)inode_index) {
            directory_index = (int)i;
            break;
        }
    }

    if (directory_index < 0)
        return VFS_ERR_CORRUPT;

    saved_inode = qfs_inodes[inode_index];
    saved_entry = qfs_directory[directory_index];

    /*
     * First remove the directory reference and make it durable.
     * The inode and its data blocks remain intact at this point.
     */
    qfs_zero(
        &qfs_directory[directory_index],
        sizeof(qfs_directory[directory_index]));

    if (qfs_save_directory() != 0 || qfs_flush() != 0)
        goto rollback;

    /*
     * Now clear the inode. Only after this is durable may its
     * data blocks be returned to the free-space bitmap.
     */
    qfs_zero(
        &qfs_inodes[inode_index],
        sizeof(qfs_inodes[inode_index]));

    if (qfs_save_inode((qm_u32_t)inode_index) != 0 ||
        qfs_flush() != 0)
        goto rollback;

    /*
     * The file is now logically deleted. Detach its VFS node.
     */
    *link = node->next;

    qfs_zero(qfs_names[inode_index], QFS_MAX_NAME);
    qfs_zero(&qfs_nodes[inode_index], sizeof(qfs_nodes[inode_index]));

    /*
     * Reclaim blocks only after durable inode removal.
     * A failure here can leak space, but cannot leave a live inode
     * pointing at a block that has already been marked free.
     */
    for (i = 0; i < QFS_DIRECT_BLOCKS; ++i) {
        if (saved_inode.blocks[i] != 0) {
            if (qfs_free_block(saved_inode.blocks[i]) != 0)
                reclaim_failed = 1;
        }
    }

    if (qfs_flush() != 0)
        reclaim_failed = 1;

    return reclaim_failed ? VFS_ERR_IO : VFS_OK;

rollback:
    /*
     * Best-effort restoration if metadata commit failed.
     */
    qfs_inodes[inode_index] = saved_inode;
    qfs_directory[directory_index] = saved_entry;

    if (qfs_save_inode((qm_u32_t)inode_index) != 0)
        rollback_failed = 1;

    if (qfs_save_directory() != 0)
        rollback_failed = 1;

    if (qfs_flush() != 0)
        rollback_failed = 1;

    return rollback_failed ? VFS_ERR_IO : VFS_ERR_IO;
}"""

    source = replace_function(
        source,
        "static int qfs_remove(",
        "const struct vfs_operations qfs_file_operations",
        remove_function
    )

    # ------------------------------------------------
    # 4. Add mount-time block consistency validation.
    # ------------------------------------------------

    anchor = r"""    /*
     * Inode zero is reserved and must never represent a file.
     */"""

    validation = r"""    /*
     * Validate inode block ownership and bitmap consistency.
     *
     * A file of N bytes must own exactly ceil(N / sector_size)
     * direct blocks. Duplicate block ownership is corruption.
     */
    {
        qm_u8_t block_seen[QFS_MAX_SECTORS];

        qfs_zero(block_seen, sizeof(block_seen));

        for (i = 0; i < QFS_MAX_INODES; ++i) {
            qm_u32_t required;
            qm_u32_t j;

            if (!qfs_inodes[i].used) {
                if (qfs_inodes[i].type != 0 ||
                    qfs_inodes[i].size != 0) {
                    qfs_is_mounted = 0;
                    return VFS_ERR_INVALID;
                }

                for (j = 0; j < QFS_DIRECT_BLOCKS; ++j) {
                    if (qfs_inodes[i].blocks[j] != 0) {
                        qfs_is_mounted = 0;
                        return VFS_ERR_INVALID;
                    }
                }

                continue;
            }

            required =
                (qfs_inodes[i].size + QFS_SECTOR_SIZE - 1U)
                / QFS_SECTOR_SIZE;

            if (required > QFS_DIRECT_BLOCKS) {
                qfs_is_mounted = 0;
                return VFS_ERR_INVALID;
            }

            for (j = 0; j < QFS_DIRECT_BLOCKS; ++j) {
                qm_u32_t block = qfs_inodes[i].blocks[j];

                if (j < required) {
                    if (block < QFS_DATA_START ||
                        block >= qfs_total_sectors ||
                        !qfs_bitmap_test(block) ||
                        block_seen[block]) {
                        qfs_is_mounted = 0;
                        return VFS_ERR_INVALID;
                    }

                    block_seen[block] = 1;
                } else if (block != 0) {
                    qfs_is_mounted = 0;
                    return VFS_ERR_INVALID;
                }
            }
        }
    }

    /*
     * Inode zero is reserved and must never represent a file.
     */"""

    if source.count(anchor) != 1:
        raise RuntimeError("Cannot locate inode-zero mount validation point")

    source = source.replace(anchor, validation, 1)

    # ------------------------------------------------
    # 5. Verify expected definitions and write once.
    # ------------------------------------------------

    checks = [
        ("static int qfs_write_file(", 1),
        ("static int qfs_create(", 1),
        ("static int qfs_remove(", 1),
        ("block_seen[QFS_MAX_SECTORS]", 1),
    ]

    for token, expected in checks:
        actual = source.count(token)
        if actual != expected:
            raise RuntimeError(
                f"Sanity check failed for {token}: {actual}"
            )

    path.write_text(source)

except Exception as error:
    sys.exit("[ERROR] " + str(error) + "\nNo changes written.")

print("[OK] File write rollback updated.")
print("[OK] File creation rollback updated.")
print("[OK] File deletion ordering updated.")
print("[OK] Mount block ownership validation added.")
print("[OK] QFS source written.")
