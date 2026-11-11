#include "qfs.h"

#define QFS_SECTOR_SIZE       512U
#define QFS_BITMAP_START      1U
#define QFS_BITMAP_SECTORS    8U
#define QFS_INODE_START       9U
#define QFS_INODE_SECTORS     8U
#define QFS_DIRECTORY_START   17U
#define QFS_DIRECTORY_SECTORS 8U
#define QFS_DATA_START        25U

#define QFS_DIRECTORY_ENTRIES 64U
#define QFS_DIRECT_BLOCKS     8U

#define QFS_MAGIC             0x31534651U
#define QFS_INODE_FILE        1U

#define QFS_ERR_IO           -20
#define QFS_ERR_CORRUPT      -21
#define QFS_ERR_LIMIT        -22

struct qfs_inode {
    qm_u8_t used;
    qm_u8_t type;
    qm_u16_t reserved;
    qm_u32_t size;
    qm_u32_t blocks[QFS_DIRECT_BLOCKS];
};

struct qfs_directory_entry {
    qm_u32_t inode;
    char name[QFS_MAX_NAME];
};

static struct vfs_node qfs_root_node;
static struct vfs_node qfs_nodes[QFS_MAX_INODES];
static char qfs_names[QFS_MAX_INODES][QFS_MAX_NAME];

static struct qfs_inode qfs_inodes[QFS_MAX_INODES];
static struct qfs_directory_entry qfs_directory[QFS_DIRECTORY_ENTRIES];

static qm_u8_t qfs_bitmap[QFS_BITMAP_SECTORS * QFS_SECTOR_SIZE];

static qm_block_device_id_t qfs_device_id;
static qm_u64_t qfs_partition_start;
static qm_u32_t qfs_total_sectors;
static int qfs_is_mounted;
static struct vfs_node *qfs_mountpoint;

static const struct vfs_filesystem qfs_descriptor = {
    .name = "qfs"
};

/* Forward declaration: qfs_create() uses this operations table. */
const struct vfs_operations qfs_file_operations;

static int qfs_string_equal(const char *a, const char *b)
{
    qm_u32_t i = 0;

    if (a == 0 || b == 0)
        return 0;

    while (a[i] && b[i]) {
        if (a[i] != b[i])
            return 0;
        ++i;
    }

    return a[i] == b[i];
}

static qm_u32_t qfs_string_length(const char *s)
{
    qm_u32_t length = 0;

    if (s == 0)
        return 0;

    while (s[length])
        ++length;

    return length;
}

static int qfs_valid_name(const char *name)
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
{
    qm_u8_t *p = (qm_u8_t *)buffer;
    qm_u32_t i;

    for (i = 0; i < size; ++i)
        p[i] = 0;
}

static void qfs_copy(void *destination, const void *source,
                     qm_u32_t size)
{
    qm_u8_t *d = (qm_u8_t *)destination;
    const qm_u8_t *s = (const qm_u8_t *)source;
    qm_u32_t i;

    for (i = 0; i < size; ++i)
        d[i] = s[i];
}

static void qfs_write_u32(qm_u8_t *p, qm_u32_t value)
{
    p[0] = (qm_u8_t)value;
    p[1] = (qm_u8_t)(value >> 8);
    p[2] = (qm_u8_t)(value >> 16);
    p[3] = (qm_u8_t)(value >> 24);
}

static qm_u32_t qfs_read_u32(const qm_u8_t *p)
{
    return (qm_u32_t)p[0] |
           ((qm_u32_t)p[1] << 8) |
           ((qm_u32_t)p[2] << 16) |
           ((qm_u32_t)p[3] << 24);
}

static int qfs_read_sector(qm_u32_t relative_lba, void *buffer)
{
    if (!qfs_is_mounted)
        return QFS_ERR_IO;

    if (relative_lba >= qfs_total_sectors)
        return QFS_ERR_CORRUPT;

    if (qm_block_read(
            qfs_device_id,
            qfs_partition_start + relative_lba,
            1,
            buffer) != 0)
        return QFS_ERR_IO;

    return 0;
}

static int qfs_write_sector(qm_u32_t relative_lba,
                            const void *buffer)
{
    if (!qfs_is_mounted)
        return QFS_ERR_IO;

    if (relative_lba >= qfs_total_sectors)
        return QFS_ERR_CORRUPT;

    if (qm_block_write(
            qfs_device_id,
            qfs_partition_start + relative_lba,
            1,
            buffer) != 0)
        return QFS_ERR_IO;

    return 0;
}

static int qfs_flush(void)
{
    if (qm_block_flush(qfs_device_id) != 0)
        return QFS_ERR_IO;

    return 0;
}

static int qfs_valid_partition(const qm_partition_t *partition)
{
    qm_block_device_info_t info;

    if (partition == 0)
        return 0;

    if (partition->sector_count < 64 ||
        partition->sector_count > QFS_MAX_SECTORS)
        return 0;

    if (qm_block_get_info(partition->device_id, &info) != 0)
        return 0;

    if (info.block_size != QFS_SECTOR_SIZE)
        return 0;

    if (partition->start_lba >= info.block_count)
        return 0;

    if (partition->sector_count >
        info.block_count - partition->start_lba)
        return 0;

    return 1;
}

static int qfs_bitmap_test(qm_u32_t sector)
{
    return (qfs_bitmap[sector / 8U] >> (sector % 8U)) & 1U;
}

static void qfs_bitmap_set(qm_u32_t sector)
{
    qfs_bitmap[sector / 8U] |=
        (qm_u8_t)(1U << (sector % 8U));
}

static void qfs_bitmap_clear(qm_u32_t sector)
{
    qfs_bitmap[sector / 8U] &=
        (qm_u8_t)~(1U << (sector % 8U));
}

static int qfs_save_bitmap(void)
{
    qm_u32_t i;

    for (i = 0; i < QFS_BITMAP_SECTORS; ++i) {
        if (qfs_write_sector(
                QFS_BITMAP_START + i,
                &qfs_bitmap[i * QFS_SECTOR_SIZE]) != 0)
            return QFS_ERR_IO;
    }

    return 0;
}

static int qfs_load_bitmap(void)
{
    qm_u32_t i;

    for (i = 0; i < QFS_BITMAP_SECTORS; ++i) {
        if (qfs_read_sector(
                QFS_BITMAP_START + i,
                &qfs_bitmap[i * QFS_SECTOR_SIZE]) != 0)
            return QFS_ERR_IO;
    }

    return 0;
}

static int qfs_save_inode(qm_u32_t index)
{
    qm_u8_t sector[QFS_SECTOR_SIZE];
    qm_u32_t sector_index;
    qm_u32_t offset;
    qm_u32_t i;
    qm_u32_t lba;

    if (index >= QFS_MAX_INODES)
        return QFS_ERR_CORRUPT;

    sector_index = index / 8U;
    offset = (index % 8U) * 64U;
    lba = QFS_INODE_START + sector_index;

    if (qfs_read_sector(lba, sector) != 0)
        return QFS_ERR_IO;

    qfs_zero(&sector[offset], 64);

    sector[offset] = qfs_inodes[index].used;
    sector[offset + 1] = qfs_inodes[index].type;
    qfs_write_u32(&sector[offset + 4], qfs_inodes[index].size);

    for (i = 0; i < QFS_DIRECT_BLOCKS; ++i)
        qfs_write_u32(
            &sector[offset + 8 + i * 4],
            qfs_inodes[index].blocks[i]);

    return qfs_write_sector(lba, sector);
}

static int qfs_load_inode(qm_u32_t index)
{
    qm_u8_t sector[QFS_SECTOR_SIZE];
    qm_u32_t sector_index;
    qm_u32_t offset;
    qm_u32_t i;

    if (index >= QFS_MAX_INODES)
        return QFS_ERR_CORRUPT;

    sector_index = index / 8U;
    offset = (index % 8U) * 64U;

    if (qfs_read_sector(
            QFS_INODE_START + sector_index,
            sector) != 0)
        return QFS_ERR_IO;

    qfs_inodes[index].used = sector[offset];
    qfs_inodes[index].type = sector[offset + 1];
    qfs_inodes[index].size =
        qfs_read_u32(&sector[offset + 4]);

    for (i = 0; i < QFS_DIRECT_BLOCKS; ++i)
        qfs_inodes[index].blocks[i] =
            qfs_read_u32(&sector[offset + 8 + i * 4]);

    if (qfs_inodes[index].used) {
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

    return 0;
}

static int qfs_save_directory(void)
{
    qm_u32_t i;
    qm_u32_t sector_index;

    for (sector_index = 0;
         sector_index < QFS_DIRECTORY_SECTORS;
         ++sector_index) {
        qm_u8_t sector[QFS_SECTOR_SIZE];

        qfs_zero(sector, sizeof(sector));

        for (i = 0; i < 8; ++i) {
            qm_u32_t index = sector_index * 8U + i;
            qm_u32_t offset = i * 64U;

            qfs_write_u32(
                &sector[offset],
                qfs_directory[index].inode);

            qfs_copy(
                &sector[offset + 4],
                qfs_directory[index].name,
                QFS_MAX_NAME);
        }

        if (qfs_write_sector(
                QFS_DIRECTORY_START + sector_index,
                sector) != 0)
            return QFS_ERR_IO;
    }

    return 0;
}

static int qfs_load_directory(void)
{
    qm_u32_t sector_index;
    qm_u32_t i;

    for (sector_index = 0;
         sector_index < QFS_DIRECTORY_SECTORS;
         ++sector_index) {
        qm_u8_t sector[QFS_SECTOR_SIZE];

        if (qfs_read_sector(
                QFS_DIRECTORY_START + sector_index,
                sector) != 0)
            return QFS_ERR_IO;

        for (i = 0; i < 8; ++i) {
            qm_u32_t index = sector_index * 8U + i;
            qm_u32_t offset = i * 64U;

            qfs_directory[index].inode =
                qfs_read_u32(&sector[offset]);

            qfs_copy(
                qfs_directory[index].name,
                &sector[offset + 4],
                QFS_MAX_NAME);

            qfs_directory[index].name[QFS_MAX_NAME - 1] = '\0';

            if (qfs_directory[index].inode != 0 &&
                qfs_directory[index].inode >= QFS_MAX_INODES)
                return QFS_ERR_CORRUPT;
        }
    }

    return 0;
}

static int qfs_allocate_block(qm_u32_t *out_sector)
{
    qm_u32_t sector;

    if (out_sector == 0)
        return QFS_ERR_CORRUPT;

    for (sector = QFS_DATA_START;
         sector < qfs_total_sectors;
         ++sector) {
        if (!qfs_bitmap_test(sector)) {
            qfs_bitmap_set(sector);

            if (qfs_save_bitmap() != 0) {
                qfs_bitmap_clear(sector);
                return QFS_ERR_IO;
            }

            *out_sector = sector;
            return 0;
        }
    }

    return VFS_ERR_NO_SPACE;
}

static int qfs_free_block(qm_u32_t sector)
{
    if (sector < QFS_DATA_START ||
        sector >= qfs_total_sectors)
        return QFS_ERR_CORRUPT;

    qfs_bitmap_clear(sector);

    return qfs_save_bitmap();
}

static int qfs_find_inode(const char *name)
{
    qm_u32_t i;

    for (i = 0; i < QFS_DIRECTORY_ENTRIES; ++i) {
        if (qfs_directory[i].inode != 0 &&
            qfs_string_equal(qfs_directory[i].name, name))
            return (int)qfs_directory[i].inode;
    }

    return -1;
}

static int qfs_find_free_directory(void)
{
    qm_u32_t i;

    for (i = 0; i < QFS_DIRECTORY_ENTRIES; ++i) {
        if (qfs_directory[i].inode == 0)
            return (int)i;
    }

    return -1;
}

static int qfs_find_free_inode(void)
{
    qm_u32_t i;

    for (i = 1; i < QFS_MAX_INODES; ++i) {
        if (!qfs_inodes[i].used)
            return (int)i;
    }

    return -1;
}

static int qfs_node_inode(struct vfs_node *node)
{
    qm_u32_t i;

    for (i = 0; i < QFS_MAX_INODES; ++i) {
        if (&qfs_nodes[i] == node)
            return (int)i;
    }

    return -1;
}

static int qfs_sync_node(qm_u32_t index)
{
    if (index >= QFS_MAX_INODES)
        return QFS_ERR_CORRUPT;

    qfs_nodes[index].size = qfs_inodes[index].size;
    return 0;
}

static int qfs_read_file(
    struct vfs_node *node,
    uint64_t offset,
    void *buffer,
    size_t size)
{
    int inode_index;
    qm_u8_t sector[QFS_SECTOR_SIZE];
    qm_u8_t *destination = (qm_u8_t *)buffer;
    qm_u32_t total;
    qm_u32_t copied = 0;

    if (node == 0 || (buffer == 0 && size != 0))
        return VFS_ERR_INVALID;

    inode_index = qfs_node_inode(node);

    if (inode_index <= 0 ||
        !qfs_inodes[inode_index].used)
        return VFS_ERR_INVALID;

    if (offset >= qfs_inodes[inode_index].size || size == 0)
        return 0;

    total = qfs_inodes[inode_index].size - (qm_u32_t)offset;

    if (size < total)
        total = (qm_u32_t)size;

    while (copied < total) {
        qm_u32_t position = (qm_u32_t)offset + copied;
        qm_u32_t logical_block = position / QFS_SECTOR_SIZE;
        qm_u32_t sector_offset = position % QFS_SECTOR_SIZE;
        qm_u32_t chunk = QFS_SECTOR_SIZE - sector_offset;
        qm_u32_t physical;

        if (logical_block >= QFS_DIRECT_BLOCKS)
            return VFS_ERR_IO;

        physical = qfs_inodes[inode_index].blocks[logical_block];

        if (physical == 0)
            return VFS_ERR_IO;

        if (qfs_read_sector(physical, sector) != 0)
            return VFS_ERR_IO;

        if (chunk > total - copied)
            chunk = total - copied;

        qfs_copy(
            destination + copied,
            sector + sector_offset,
            chunk);

        copied += chunk;
    }

    return (int)copied;
}

static int qfs_write_file(
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
}

static int qfs_lookup(
    struct vfs_node *directory,
    const char *name,
    struct vfs_node **result)
{
    int inode_index;

    if (directory != &qfs_root_node ||
        name == 0 || result == 0)
        return VFS_ERR_INVALID;

    inode_index = qfs_find_inode(name);

    if (inode_index < 0) {
        *result = 0;
        return VFS_ERR_NOT_FOUND;
    }

    *result = &qfs_nodes[inode_index];
    return VFS_OK;
}

static int qfs_create(
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
}

static int qfs_remove(
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
        return QFS_ERR_CORRUPT;

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
}

const struct vfs_operations qfs_file_operations = {
    .read = qfs_read_file,
    .write = qfs_write_file
};

static const struct vfs_operations qfs_directory_operations = {
    .lookup = qfs_lookup,
    .create = qfs_create,
    .remove = qfs_remove
};

static int qfs_load_nodes(void)
{
    qm_u32_t i;

    qfs_root_node.name = "";
    qfs_root_node.type = VFS_NODE_DIRECTORY;
    qfs_root_node.mode = 0755U;
    qfs_root_node.size = 0;
    qfs_root_node.parent = &qfs_root_node;
    qfs_root_node.children = 0;
    qfs_root_node.next = 0;
    qfs_root_node.mounted_root = 0;
    qfs_root_node.operations = &qfs_directory_operations;
    qfs_root_node.private_data = 0;

    for (i = 0; i < QFS_MAX_INODES; ++i) {
        qfs_zero(&qfs_nodes[i], sizeof(qfs_nodes[i]));
        qfs_zero(qfs_names[i], QFS_MAX_NAME);

        if (i == 0)
            continue;

        if (!qfs_inodes[i].used)
            continue;

        {
            qm_u32_t j;
            int directory_found = 0;

            for (j = 0; j < QFS_DIRECTORY_ENTRIES; ++j) {
                if (qfs_directory[j].inode == i) {
                    qfs_copy(
                        qfs_names[i],
                        qfs_directory[j].name,
                        QFS_MAX_NAME);
                    qfs_names[i][QFS_MAX_NAME - 1] = '\0';
                    directory_found = 1;
                    break;
                }
            }

            if (!directory_found)
                return QFS_ERR_CORRUPT;
        }

        if (qfs_names[i][0] == '\0')
            return QFS_ERR_CORRUPT;

        qfs_nodes[i].name = qfs_names[i];
        qfs_nodes[i].type = VFS_NODE_FILE;
        qfs_nodes[i].mode = 0644U;
        qfs_nodes[i].size = qfs_inodes[i].size;
        qfs_nodes[i].parent = &qfs_root_node;
        qfs_nodes[i].operations = &qfs_file_operations;

        if (qfs_root_node.children == 0) {
            qfs_root_node.children = &qfs_nodes[i];
        } else {
            struct vfs_node *tail = qfs_root_node.children;

            while (tail->next != 0)
                tail = tail->next;

            tail->next = &qfs_nodes[i];
        }
    }

    return 0;
}

int qfs_format(const qm_partition_t *partition)
{
    qm_u8_t sector[QFS_SECTOR_SIZE];
    qm_u32_t i;

    if (qfs_is_mounted)
        return VFS_ERR_BUSY;

    if (!qfs_valid_partition(partition))
        return VFS_ERR_INVALID;

    qfs_device_id = partition->device_id;
    qfs_partition_start = partition->start_lba;
    qfs_total_sectors = (qm_u32_t)partition->sector_count;

    /*
     * Formatting is allowed before mounting, so temporarily enable
     * internal sector I/O.
     */
    qfs_is_mounted = 1;

    qfs_zero(qfs_bitmap, sizeof(qfs_bitmap));
    qfs_zero(qfs_inodes, sizeof(qfs_inodes));
    qfs_zero(qfs_directory, sizeof(qfs_directory));

    for (i = 0; i < QFS_DATA_START; ++i)
        qfs_bitmap_set(i);

    if (qfs_save_bitmap() != 0) {
        qfs_is_mounted = 0;
        return VFS_ERR_IO;
    }

    for (i = 0; i < QFS_INODE_SECTORS; ++i) {
        qfs_zero(sector, sizeof(sector));

        if (qfs_write_sector(QFS_INODE_START + i, sector) != 0) {
            qfs_is_mounted = 0;
            return VFS_ERR_IO;
        }
    }

    if (qfs_save_directory() != 0) {
        qfs_is_mounted = 0;
        return VFS_ERR_IO;
    }

    qfs_zero(sector, sizeof(sector));
    qfs_write_u32(&sector[0], QFS_MAGIC);
    qfs_write_u32(&sector[4], QFS_VERSION);
    qfs_write_u32(&sector[8], qfs_total_sectors);
    qfs_write_u32(&sector[12], QFS_DATA_START);
    qfs_write_u32(&sector[16], QFS_MAX_INODES);
    qfs_write_u32(&sector[20], QFS_MAX_FILE_SIZE);
    qfs_write_u32(&sector[24], QFS_BITMAP_START);
    qfs_write_u32(&sector[28], QFS_INODE_START);
    qfs_write_u32(&sector[32], QFS_DIRECTORY_START);

    if (qfs_write_sector(0, sector) != 0 ||
        qfs_flush() != 0) {
        qfs_is_mounted = 0;
        return VFS_ERR_IO;
    }

    qfs_is_mounted = 0;
    return VFS_OK;
}

int qfs_mount(const qm_partition_t *partition)
{
    qm_u8_t sector[QFS_SECTOR_SIZE];
    qm_u32_t i;

    if (qfs_is_mounted)
        return VFS_ERR_BUSY;

    if (!qfs_valid_partition(partition))
        return VFS_ERR_INVALID;

    qfs_device_id = partition->device_id;
    qfs_partition_start = partition->start_lba;
    qfs_total_sectors = (qm_u32_t)partition->sector_count;
    qfs_mountpoint = 0;
    qfs_root_node.parent = &qfs_root_node;
    qfs_root_node.children = 0;

    /*
     * Enable bounded reads while validating the superblock.
     */
    qfs_is_mounted = 1;

    if (qfs_read_sector(0, sector) != 0) {
        qfs_is_mounted = 0;
        return VFS_ERR_IO;
    }

    if (qfs_read_u32(&sector[0]) != QFS_MAGIC ||
        qfs_read_u32(&sector[4]) != QFS_VERSION ||
        qfs_read_u32(&sector[8]) != qfs_total_sectors ||
        qfs_read_u32(&sector[12]) != QFS_DATA_START ||
        qfs_read_u32(&sector[16]) != QFS_MAX_INODES ||
        qfs_read_u32(&sector[20]) != QFS_MAX_FILE_SIZE ||
        qfs_read_u32(&sector[24]) != QFS_BITMAP_START ||
        qfs_read_u32(&sector[28]) != QFS_INODE_START ||
        qfs_read_u32(&sector[32]) != QFS_DIRECTORY_START) {
        qfs_is_mounted = 0;
        return VFS_ERR_INVALID;
    }

    if (qfs_load_bitmap() != 0 ||
        qfs_load_directory() != 0) {
        qfs_is_mounted = 0;
        return VFS_ERR_IO;
    }

    for (i = 0; i < QFS_DATA_START; ++i) {
        if (!qfs_bitmap_test(i)) {
            qfs_is_mounted = 0;
            return VFS_ERR_INVALID;
        }
    }

    for (i = 0; i < QFS_MAX_INODES; ++i) {
        if (qfs_load_inode(i) != 0) {
            qfs_is_mounted = 0;
            return VFS_ERR_INVALID;
        }
    }

    /*
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
     */
    if (qfs_inodes[0].used) {
        qfs_is_mounted = 0;
        return VFS_ERR_INVALID;
    }

    /*
     * Validate directory references and reject duplicate names
     * or duplicate inode references before building VFS nodes.
     */
    for (i = 0; i < QFS_DIRECTORY_ENTRIES; ++i) {
        if (qfs_directory[i].inode != 0) {
            qm_u32_t inode = qfs_directory[i].inode;
            qm_u32_t j;

            if (inode >= QFS_MAX_INODES ||
                !qfs_inodes[inode].used ||
                qfs_inodes[inode].type != QFS_INODE_FILE ||
                !qfs_valid_name(qfs_directory[i].name)) {
                qfs_is_mounted = 0;
                return VFS_ERR_INVALID;
            }

            for (j = i + 1; j < QFS_DIRECTORY_ENTRIES; ++j) {
                if (qfs_directory[j].inode == 0)
                    continue;

                if (qfs_directory[j].inode == inode ||
                    qfs_string_equal(
                        qfs_directory[i].name,
                        qfs_directory[j].name)) {
                    qfs_is_mounted = 0;
                    return VFS_ERR_INVALID;
                }
            }
        }
    }

    if (qfs_load_nodes() != 0) {
        qfs_is_mounted = 0;
        return VFS_ERR_INVALID;
    }

    return VFS_OK;
}

int qfs_mount_at(
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

int qfs_unmount(void)
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

struct vfs_node *qfs_root(void)
{
    if (!qfs_is_mounted)
        return 0;

    return &qfs_root_node;
}

const struct vfs_filesystem *qfs_filesystem(void)
{
    return &qfs_descriptor;
}
