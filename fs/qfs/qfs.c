#include "qfs.h"

#define QFS_SECTOR_SIZE       512U
#define QFS_BITMAP_START      1U
#define QFS_BITMAP_SECTORS    8U
#define QFS_INODE_START       9U
#define QFS_INODE_SECTORS     8U
#define QFS_DIRECTORY_START   17U
#define QFS_DIRECTORY_SECTORS 8U

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
static int qfs_read_only;
static int qfs_needs_recovery;
static qm_u32_t qfs_format_version;
static qm_u32_t qfs_data_start = QFS_DATA_START;
static int qfs_tx_active;
static qm_u32_t qfs_tx_count;
static qm_u32_t qfs_tx_sequence;
struct qfs_tx_entry {
    qm_u32_t lba;
    qm_u8_t data[QFS_SECTOR_SIZE];
};
static struct qfs_tx_entry qfs_tx_entries[QFS_JOURNAL_MAX_ENTRIES];
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

static int qfs_read_sector_raw(qm_u32_t relative_lba, void *buffer)
{
    if (!qfs_is_mounted)
        return QFS_ERR_IO;
    if (relative_lba >= qfs_total_sectors)
        return QFS_ERR_CORRUPT;
    if (qm_block_read(qfs_device_id,
                      qfs_partition_start + relative_lba,
                      1, buffer) != 0)
        return QFS_ERR_IO;
    return 0;
}

static int qfs_write_sector_raw(qm_u32_t relative_lba,
                                const void *buffer)
{
    if (!qfs_is_mounted)
        return QFS_ERR_IO;
    if (relative_lba >= qfs_total_sectors)
        return QFS_ERR_CORRUPT;
    if (qm_block_write(qfs_device_id,
                       qfs_partition_start + relative_lba,
                       1, buffer) != 0)
        return QFS_ERR_IO;
    return 0;
}

static int qfs_read_sector(qm_u32_t relative_lba, void *buffer)
{
    qm_u32_t i;
    if (!qfs_is_mounted || qfs_needs_recovery)
        return QFS_ERR_IO;
    if (relative_lba >= qfs_total_sectors)
        return QFS_ERR_CORRUPT;
    if (qfs_tx_active) {
        for (i = 0; i < qfs_tx_count; ++i) {
            if (qfs_tx_entries[i].lba == relative_lba) {
                qfs_copy(buffer, qfs_tx_entries[i].data, QFS_SECTOR_SIZE);
                return 0;
            }
        }
    }
    return qfs_read_sector_raw(relative_lba, buffer);
}

static int qfs_write_sector(qm_u32_t relative_lba,
                            const void *buffer)
{
    qm_u32_t i;
    if (!qfs_is_mounted || qfs_needs_recovery)
        return QFS_ERR_IO;
    if (relative_lba >= qfs_total_sectors)
        return QFS_ERR_CORRUPT;
    if (qfs_tx_active) {
        if (relative_lba == 0 ||
            (relative_lba >= QFS_JOURNAL_START &&
             relative_lba < QFS_JOURNAL_START + QFS_JOURNAL_SECTORS))
            return QFS_ERR_CORRUPT;
        for (i = 0; i < qfs_tx_count; ++i) {
            if (qfs_tx_entries[i].lba == relative_lba) {
                qfs_copy(qfs_tx_entries[i].data, buffer, QFS_SECTOR_SIZE);
                return 0;
            }
        }
        if (qfs_tx_count >= QFS_JOURNAL_MAX_ENTRIES)
            return QFS_ERR_LIMIT;
        qfs_tx_entries[qfs_tx_count].lba = relative_lba;
        qfs_copy(qfs_tx_entries[qfs_tx_count].data, buffer, QFS_SECTOR_SIZE);
        ++qfs_tx_count;
        return 0;
    }
    return qfs_write_sector_raw(relative_lba, buffer);
}

static int qfs_flush(void)
{
    if (qm_block_flush(qfs_device_id) != 0)
        return QFS_ERR_IO;

    return 0;
}

/* QFS v2 redo journal. All journal I/O bypasses the transaction overlay. */
#define QFS_JOURNAL_HEADER_MAGIC 0x324C4E4AU
#define QFS_JOURNAL_COMMIT_MAGIC 0x324D434AU
#define QFS_JOURNAL_FORMAT       1U
#define QFS_JOURNAL_HEADER_LBA   QFS_JOURNAL_START
#define QFS_JOURNAL_PAYLOAD_LBA  (QFS_JOURNAL_START + 1U)
#define QFS_JOURNAL_COMMIT_A     (QFS_JOURNAL_START + 1U + QFS_JOURNAL_MAX_ENTRIES)
#define QFS_JOURNAL_COMMIT_B     (QFS_JOURNAL_COMMIT_A + 1U)
#define QFS_JOURNAL_CRC_OFFSET   508U

static qm_u32_t qfs_crc32(const qm_u8_t *data, qm_u32_t size)
{
    qm_u32_t crc = 0xFFFFFFFFU;
    qm_u32_t i;
    qm_u32_t bit;

    for (i = 0; i < size; ++i) {
        crc ^= data[i];
        for (bit = 0; bit < 8U; ++bit)
            crc = (crc >> 1) ^
                (0xEDB88320U & (qm_u32_t)-(int)(crc & 1U));
    }
    return ~crc;
}

static int qfs_is_zero_sector(const qm_u8_t *sector)
{
    qm_u32_t i;
    for (i = 0; i < QFS_SECTOR_SIZE; ++i) {
        if (sector[i] != 0)
            return 0;
    }
    return 1;
}

static qm_u32_t qfs_sector_crc(const qm_u8_t *sector)
{
    return qfs_crc32(sector, QFS_SECTOR_SIZE);
}

static int qfs_check_record_crc(const qm_u8_t *record)
{
    qm_u8_t copy[QFS_SECTOR_SIZE];
    qm_u32_t stored;
    qfs_copy(copy, record, QFS_SECTOR_SIZE);
    stored = qfs_read_u32(&copy[QFS_JOURNAL_CRC_OFFSET]);
    qfs_write_u32(&copy[QFS_JOURNAL_CRC_OFFSET], 0);
    return stored == qfs_sector_crc(copy);
}

static int qfs_commit_record_valid(const qm_u8_t *record)
{
    return qfs_read_u32(&record[0]) == QFS_JOURNAL_COMMIT_MAGIC &&
           qfs_read_u32(&record[4]) == QFS_JOURNAL_FORMAT &&
           qfs_read_u32(&record[8]) != 0 &&
           qfs_read_u32(&record[16]) > 0 &&
           qfs_read_u32(&record[16]) <= QFS_JOURNAL_MAX_ENTRIES &&
           qfs_check_record_crc(record);
}

static int qfs_journal_clear(void)
{
    qm_u8_t zero[QFS_SECTOR_SIZE];
    qfs_zero(zero, sizeof(zero));
    if (qfs_write_sector_raw(QFS_JOURNAL_COMMIT_A, zero) != 0 ||
        qfs_write_sector_raw(QFS_JOURNAL_COMMIT_B, zero) != 0 ||
        qfs_flush() != 0)
        return QFS_ERR_IO;
    return 0;
}

static int qfs_journal_recover(void)
{
    qm_u8_t marker_a[QFS_SECTOR_SIZE];
    qm_u8_t marker_b[QFS_SECTOR_SIZE];
    qm_u8_t header[QFS_SECTOR_SIZE];
    qm_u8_t payload[QFS_SECTOR_SIZE];
    qm_u32_t txid;
    qm_u32_t header_crc;
    qm_u32_t count;
    qm_u32_t i;
    int valid_a;
    int valid_b;
    const qm_u8_t *marker;

    if (qfs_read_sector_raw(QFS_JOURNAL_COMMIT_A, marker_a) != 0 ||
        qfs_read_sector_raw(QFS_JOURNAL_COMMIT_B, marker_b) != 0)
        return QFS_ERR_IO;

    if (qfs_is_zero_sector(marker_a) && qfs_is_zero_sector(marker_b))
        return 0;

    valid_a = qfs_commit_record_valid(marker_a);
    valid_b = qfs_commit_record_valid(marker_b);
    if (!valid_a && !valid_b)
        return QFS_ERR_CORRUPT;

    if (valid_a && valid_b &&
        (qfs_read_u32(&marker_a[8]) != qfs_read_u32(&marker_b[8]) ||
         qfs_read_u32(&marker_a[12]) != qfs_read_u32(&marker_b[12]) ||
         qfs_read_u32(&marker_a[16]) != qfs_read_u32(&marker_b[16])))
        return QFS_ERR_CORRUPT;

    marker = valid_a ? marker_a : marker_b;
    txid = qfs_read_u32(&marker[8]);
    header_crc = qfs_read_u32(&marker[12]);
    count = qfs_read_u32(&marker[16]);

    if (qfs_read_sector_raw(QFS_JOURNAL_HEADER_LBA, header) != 0)
        return QFS_ERR_IO;
    if (qfs_read_u32(&header[0]) != QFS_JOURNAL_HEADER_MAGIC ||
        qfs_read_u32(&header[4]) != QFS_JOURNAL_FORMAT ||
        qfs_read_u32(&header[8]) != txid ||
        qfs_read_u32(&header[12]) != count ||
        qfs_read_u32(&header[QFS_JOURNAL_CRC_OFFSET]) != header_crc ||
        !qfs_check_record_crc(header))
        return QFS_ERR_CORRUPT;

    for (i = 0; i < count; ++i) {
        qm_u32_t offset = 16U + i * 8U;
        qm_u32_t home = qfs_read_u32(&header[offset]);
        qm_u32_t expected_crc = qfs_read_u32(&header[offset + 4U]);
        qm_u32_t j;

        if (!((home >= QFS_BITMAP_START && home < QFS_JOURNAL_START) ||
              (home >= qfs_data_start && home < qfs_total_sectors)))
            return QFS_ERR_CORRUPT;

        for (j = 0; j < i; ++j) {
            if (qfs_read_u32(&header[16U + j * 8U]) == home)
                return QFS_ERR_CORRUPT;
        }

        if (qfs_read_sector_raw(QFS_JOURNAL_PAYLOAD_LBA + i,
                                payload) != 0)
            return QFS_ERR_IO;
        if (qfs_sector_crc(payload) != expected_crc)
            return QFS_ERR_CORRUPT;
    }

    /* Replay is idempotent: every payload is a complete sector after-image. */
    for (i = 0; i < count; ++i) {
        qm_u32_t offset = 16U + i * 8U;
        qm_u32_t home = qfs_read_u32(&header[offset]);
        if (qfs_read_sector_raw(QFS_JOURNAL_PAYLOAD_LBA + i,
                                payload) != 0 ||
            qfs_write_sector_raw(home, payload) != 0) {
            qfs_needs_recovery = 1;
            return QFS_ERR_IO;
        }
    }

    if (qfs_flush() != 0) {
        qfs_needs_recovery = 1;
        return QFS_ERR_IO;
    }

    if (qfs_journal_clear() != 0) {
        qfs_needs_recovery = 1;
        return QFS_ERR_IO;
    }

    qfs_needs_recovery = 0;
    return 0;
}

static int qfs_tx_begin(void)
{
    if (!qfs_is_mounted || qfs_read_only || qfs_needs_recovery ||
        qfs_format_version != QFS_VERSION || qfs_tx_active)
        return QFS_ERR_IO;
    qfs_tx_active = 1;
    qfs_tx_count = 0;
    return 0;
}

static void qfs_tx_abort(void)
{
    qfs_tx_active = 0;
    qfs_tx_count = 0;
}

static int qfs_tx_commit(void)
{
    qm_u8_t header[QFS_SECTOR_SIZE];
    qm_u8_t marker[QFS_SECTOR_SIZE];
    qm_u8_t zero[QFS_SECTOR_SIZE];
    qm_u8_t old_marker[QFS_SECTOR_SIZE];
    qm_u32_t i;
    qm_u32_t txid;
    qm_u32_t header_crc;

    if (!qfs_tx_active || qfs_needs_recovery)
        return QFS_ERR_IO;

    if (qfs_tx_count == 0) {
        qfs_tx_abort();
        return 0;
    }

    if (qfs_read_sector_raw(QFS_JOURNAL_COMMIT_A, old_marker) != 0)
        goto uncertain_failure;
    if (!qfs_is_zero_sector(old_marker)) {
        qfs_needs_recovery = 1;
        qfs_root_node.children = 0;
        goto prepare_failed;
    }
    if (qfs_read_sector_raw(QFS_JOURNAL_COMMIT_B, old_marker) != 0)
        goto uncertain_failure;
    if (!qfs_is_zero_sector(old_marker)) {
        qfs_needs_recovery = 1;
        qfs_root_node.children = 0;
        goto prepare_failed;
    }

    qfs_zero(header, sizeof(header));
    ++qfs_tx_sequence;
    if (qfs_tx_sequence == 0)
        ++qfs_tx_sequence;
    txid = qfs_tx_sequence;
    qfs_write_u32(&header[0], QFS_JOURNAL_HEADER_MAGIC);
    qfs_write_u32(&header[4], QFS_JOURNAL_FORMAT);
    qfs_write_u32(&header[8], txid);
    qfs_write_u32(&header[12], qfs_tx_count);

    for (i = 0; i < qfs_tx_count; ++i) {
        qm_u32_t offset = 16U + i * 8U;
        qfs_write_u32(&header[offset], qfs_tx_entries[i].lba);
        qfs_write_u32(&header[offset + 4U],
                      qfs_sector_crc(qfs_tx_entries[i].data));
        if (qfs_write_sector_raw(QFS_JOURNAL_PAYLOAD_LBA + i,
                                 qfs_tx_entries[i].data) != 0)
            goto prepare_failed;
    }

    qfs_write_u32(&header[QFS_JOURNAL_CRC_OFFSET], 0);
    header_crc = qfs_sector_crc(header);
    qfs_write_u32(&header[QFS_JOURNAL_CRC_OFFSET], header_crc);

    if (qfs_write_sector_raw(QFS_JOURNAL_HEADER_LBA, header) != 0 ||
        qfs_flush() != 0)
        goto prepare_failed;

    qfs_zero(marker, sizeof(marker));
    qfs_write_u32(&marker[0], QFS_JOURNAL_COMMIT_MAGIC);
    qfs_write_u32(&marker[4], QFS_JOURNAL_FORMAT);
    qfs_write_u32(&marker[8], txid);
    qfs_write_u32(&marker[12], header_crc);
    qfs_write_u32(&marker[16], qfs_tx_count);
    qfs_write_u32(&marker[QFS_JOURNAL_CRC_OFFSET], 0);
    qfs_write_u32(&marker[QFS_JOURNAL_CRC_OFFSET],
                  qfs_sector_crc(marker));

    /* Either valid commit copy is sufficient for replay. */
    if (qfs_write_sector_raw(QFS_JOURNAL_COMMIT_A, marker) != 0)
        goto uncertain_failure;
    if (qfs_write_sector_raw(QFS_JOURNAL_COMMIT_B, marker) != 0)
        goto uncertain_failure;
    if (qfs_flush() != 0)
        goto uncertain_failure;

    for (i = 0; i < qfs_tx_count; ++i) {
        if (qfs_write_sector_raw(qfs_tx_entries[i].lba,
                                 qfs_tx_entries[i].data) != 0)
            goto uncertain_failure;
    }
    if (qfs_flush() != 0)
        goto uncertain_failure;

    qfs_zero(zero, sizeof(zero));
    if (qfs_write_sector_raw(QFS_JOURNAL_COMMIT_A, zero) != 0 ||
        qfs_write_sector_raw(QFS_JOURNAL_COMMIT_B, zero) != 0 ||
        qfs_flush() != 0)
        goto uncertain_failure;

    qfs_tx_abort();
    return 0;

prepare_failed:
    qfs_tx_abort();
    return QFS_ERR_IO;

uncertain_failure:
    qfs_needs_recovery = 1;
    qfs_root_node.children = 0;
    qfs_tx_abort();
    return QFS_ERR_IO;
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

    for (i = 0; i < (qfs_tx_active ? 1U : QFS_BITMAP_SECTORS); ++i) {
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
                (block < qfs_data_start ||
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

    for (sector = qfs_data_start;
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

    if (qfs_needs_recovery)
        return VFS_ERR_IO;
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

    if (qfs_needs_recovery)
        return VFS_ERR_IO;
    if (qfs_read_only)
        return VFS_ERR_NOT_SUPPORTED;
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

    if (qfs_tx_begin() != 0)
        return VFS_ERR_IO;

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

    if (qfs_tx_commit() != 0) {
        qfs_inodes[inode_index] = original_inode;
        for (i = 0; i < new_count; ++i)
            qfs_bitmap_clear(new_blocks[i]);
        node->size = original_inode.size;
        return VFS_ERR_IO;
    }

    if (qfs_sync_node((qm_u32_t)inode_index) != 0)
        return VFS_ERR_IO;

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

    if (qfs_save_inode((qm_u32_t)inode_index) != 0)
        failure = VFS_ERR_IO;

    node->size = original_inode.size;

    for (i = 0; i < new_count; ++i) {
        if (qfs_free_block(new_blocks[i]) != 0)
            failure = VFS_ERR_IO;
    }

    /* All rollback writes are still staged in RAM; discard them atomically. */
    qfs_tx_abort();
    return failure;
}

static int qfs_lookup(
    struct vfs_node *directory,
    const char *name,
    struct vfs_node **result)
{
    int inode_index;

    if (qfs_needs_recovery)
        return VFS_ERR_IO;
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
    if (directory != &qfs_root_node ||
        name == 0 || result == 0)
        return VFS_ERR_INVALID;
    if (qfs_needs_recovery)
        return VFS_ERR_IO;
    if (qfs_read_only)
        return VFS_ERR_NOT_SUPPORTED;

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

    if (qfs_tx_begin() != 0)
        return VFS_ERR_IO;

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

    /* Both metadata changes become visible together through the redo log. */
    if (qfs_save_inode((qm_u32_t)inode_index) != 0 ||
        qfs_save_directory() != 0)
        goto rollback;

    if (qfs_tx_commit() != 0) {
        qfs_zero(&qfs_inodes[inode_index], sizeof(qfs_inodes[inode_index]));
        qfs_zero(&qfs_directory[directory_index],
                 sizeof(qfs_directory[directory_index]));
        qfs_zero(qfs_names[inode_index], QFS_MAX_NAME);
        return VFS_ERR_IO;
    }

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

    qfs_zero(qfs_names[inode_index], QFS_MAX_NAME);
    qfs_zero(&qfs_nodes[inode_index], sizeof(qfs_nodes[inode_index]));
    qfs_tx_abort();
    return VFS_ERR_IO;
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

    if (qfs_needs_recovery)
        return VFS_ERR_IO;
    if (qfs_read_only)
        return VFS_ERR_NOT_SUPPORTED;
    if (qfs_tx_begin() != 0)
        return VFS_ERR_IO;

    saved_inode = qfs_inodes[inode_index];
    saved_entry = qfs_directory[directory_index];

    qfs_zero(&qfs_directory[directory_index],
             sizeof(qfs_directory[directory_index]));
    qfs_zero(&qfs_inodes[inode_index], sizeof(qfs_inodes[inode_index]));

    for (i = 0; i < QFS_DIRECT_BLOCKS; ++i) {
        if (saved_inode.blocks[i] != 0 &&
            qfs_free_block(saved_inode.blocks[i]) != 0)
            goto rollback;
    }

    if (qfs_save_inode((qm_u32_t)inode_index) != 0 ||
        qfs_save_directory() != 0)
        goto rollback;

    if (qfs_tx_commit() != 0) {
        qfs_inodes[inode_index] = saved_inode;
        qfs_directory[directory_index] = saved_entry;
        for (i = 0; i < QFS_DIRECT_BLOCKS; ++i) {
            if (saved_inode.blocks[i] != 0)
                qfs_bitmap_set(saved_inode.blocks[i]);
        }
        return VFS_ERR_IO;
    }

    *link = node->next;
    qfs_zero(qfs_names[inode_index], QFS_MAX_NAME);
    qfs_zero(&qfs_nodes[inode_index], sizeof(qfs_nodes[inode_index]));
    return VFS_OK;

rollback:
    qfs_inodes[inode_index] = saved_inode;
    qfs_directory[directory_index] = saved_entry;
    for (i = 0; i < QFS_DIRECT_BLOCKS; ++i) {
        if (saved_inode.blocks[i] != 0)
            qfs_bitmap_set(saved_inode.blocks[i]);
    }
    qfs_tx_abort();
    return VFS_ERR_IO;
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
    if (partition->sector_count <= QFS_DATA_START)
        return VFS_ERR_INVALID;

    qfs_device_id = partition->device_id;
    qfs_partition_start = partition->start_lba;
    qfs_total_sectors = (qm_u32_t)partition->sector_count;
    qfs_format_version = QFS_VERSION;
    qfs_data_start = QFS_DATA_START;
    qfs_read_only = 0;
    qfs_needs_recovery = 0;
    qfs_tx_active = 0;
    qfs_tx_count = 0;
    qfs_is_mounted = 1;

    /* v2 requires an actual device flush implementation. */
    if (qfs_flush() != 0) {
        qfs_is_mounted = 0;
        return VFS_ERR_IO;
    }

    /* Make an old v1/v2 superblock unmountable before overwriting its metadata. */
    qfs_zero(sector, sizeof(sector));
    if (qfs_write_sector_raw(0, sector) != 0 || qfs_flush() != 0) {
        qfs_is_mounted = 0;
        return VFS_ERR_IO;
    }

    qfs_zero(qfs_bitmap, sizeof(qfs_bitmap));
    qfs_zero(qfs_inodes, sizeof(qfs_inodes));
    qfs_zero(qfs_directory, sizeof(qfs_directory));
    for (i = 0; i < qfs_data_start; ++i)
        qfs_bitmap_set(i);

    /* Clear the complete journal area, including both commit records. */
    for (i = 0; i < QFS_JOURNAL_SECTORS; ++i) {
        qfs_zero(sector, sizeof(sector));
        if (qfs_write_sector_raw(QFS_JOURNAL_START + i, sector) != 0) {
            qfs_is_mounted = 0;
            return VFS_ERR_IO;
        }
    }
    if (qfs_save_bitmap() != 0) {
        qfs_is_mounted = 0;
        return VFS_ERR_IO;
    }
    for (i = 0; i < QFS_INODE_SECTORS; ++i) {
        qfs_zero(sector, sizeof(sector));
        if (qfs_write_sector_raw(QFS_INODE_START + i, sector) != 0) {
            qfs_is_mounted = 0;
            return VFS_ERR_IO;
        }
    }
    if (qfs_save_directory() != 0 || qfs_flush() != 0) {
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
    qfs_write_u32(&sector[36], QFS_JOURNAL_START);
    qfs_write_u32(&sector[40], QFS_JOURNAL_SECTORS);
    qfs_write_u32(&sector[44], QFS_JOURNAL_MAX_ENTRIES);
    qfs_write_u32(&sector[48], 1U); /* redo-journal feature bit */
    qfs_write_u32(&sector[QFS_JOURNAL_CRC_OFFSET], 0);
    qfs_write_u32(&sector[QFS_JOURNAL_CRC_OFFSET],
                  qfs_sector_crc(sector));

    /* The valid v2 superblock is the final format write. */
    if (qfs_write_sector_raw(0, sector) != 0 || qfs_flush() != 0) {
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
    int journal_status;

    if (qfs_is_mounted)
        return VFS_ERR_BUSY;

    if (!qfs_valid_partition(partition))
        return VFS_ERR_INVALID;

    qfs_device_id = partition->device_id;
    qfs_partition_start = partition->start_lba;
    qfs_total_sectors = (qm_u32_t)partition->sector_count;
    qfs_format_version = 0;
    qfs_data_start = QFS_LEGACY_DATA_START;
    qfs_read_only = 0;
    qfs_needs_recovery = 0;
    qfs_tx_active = 0;
    qfs_tx_count = 0;
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

    qfs_format_version = qfs_read_u32(&sector[4]);
    if (qfs_read_u32(&sector[0]) != QFS_MAGIC ||
        qfs_read_u32(&sector[8]) != qfs_total_sectors ||
        qfs_read_u32(&sector[16]) != QFS_MAX_INODES ||
        qfs_read_u32(&sector[20]) != QFS_MAX_FILE_SIZE ||
        qfs_read_u32(&sector[24]) != QFS_BITMAP_START ||
        qfs_read_u32(&sector[28]) != QFS_INODE_START ||
        qfs_read_u32(&sector[32]) != QFS_DIRECTORY_START) {
        qfs_is_mounted = 0;
        return VFS_ERR_INVALID;
    }

    if (qfs_format_version == QFS_LEGACY_VERSION) {
        if (qfs_read_u32(&sector[12]) != QFS_LEGACY_DATA_START) {
            qfs_is_mounted = 0;
            return VFS_ERR_INVALID;
        }
        qfs_data_start = QFS_LEGACY_DATA_START;
        qfs_read_only = 1;
    } else if (qfs_format_version == QFS_VERSION) {
        if (qfs_read_u32(&sector[12]) != QFS_DATA_START ||
            qfs_read_u32(&sector[36]) != QFS_JOURNAL_START ||
            qfs_read_u32(&sector[40]) != QFS_JOURNAL_SECTORS ||
            qfs_read_u32(&sector[44]) != QFS_JOURNAL_MAX_ENTRIES ||
            qfs_read_u32(&sector[48]) != 1U ||
            !qfs_check_record_crc(sector)) {
            qfs_is_mounted = 0;
            return VFS_ERR_INVALID;
        }
        qfs_data_start = QFS_DATA_START;
        qfs_read_only = 0;
        if (qfs_flush() != 0) {
            qfs_is_mounted = 0;
            return VFS_ERR_IO;
        }
        journal_status = qfs_journal_recover();
        if (journal_status != 0) {
            qfs_is_mounted = 0;
            return journal_status == QFS_ERR_CORRUPT
                ? VFS_ERR_INVALID : VFS_ERR_IO;
        }
    } else {
        qfs_is_mounted = 0;
        return VFS_ERR_NOT_SUPPORTED;
    }

    if (qfs_load_bitmap() != 0 ||
        qfs_load_directory() != 0) {
        qfs_is_mounted = 0;
        return VFS_ERR_IO;
    }

    for (i = 0; i < qfs_data_start; ++i) {
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
                    if (block < qfs_data_start ||
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

        /* Every allocated data bit must belong to exactly one inode. */
        for (i = qfs_data_start; i < qfs_total_sectors; ++i) {
            if (qfs_bitmap_test(i) != (block_seen[i] != 0)) {
                qfs_is_mounted = 0;
                return VFS_ERR_INVALID;
            }
        }

        /* Bits beyond the partition boundary must never be allocated. */
        for (i = qfs_total_sectors;
             i < QFS_BITMAP_SECTORS * QFS_SECTOR_SIZE * 8U;
             ++i) {
            if (qfs_bitmap_test(i)) {
                qfs_is_mounted = 0;
                return VFS_ERR_INVALID;
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

    if (qfs_needs_recovery || qfs_tx_active)
        return VFS_ERR_IO;
    if (!qfs_read_only && qfs_flush() != 0)
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
    if (!qfs_is_mounted || qfs_needs_recovery)
        return 0;

    return &qfs_root_node;
}


#ifdef QFS_TESTING
void qfs_test_reset_state(void)
{
    qfs_zero(&qfs_root_node, sizeof(qfs_root_node));
    qfs_zero(qfs_nodes, sizeof(qfs_nodes));
    qfs_zero(qfs_names, sizeof(qfs_names));
    qfs_zero(qfs_inodes, sizeof(qfs_inodes));
    qfs_zero(qfs_directory, sizeof(qfs_directory));
    qfs_zero(qfs_bitmap, sizeof(qfs_bitmap));
    qfs_zero(qfs_tx_entries, sizeof(qfs_tx_entries));
    qfs_device_id = 0;
    qfs_partition_start = 0;
    qfs_total_sectors = 0;
    qfs_is_mounted = 0;
    qfs_read_only = 0;
    qfs_needs_recovery = 0;
    qfs_format_version = 0;
    qfs_data_start = QFS_DATA_START;
    qfs_tx_active = 0;
    qfs_tx_count = 0;
    qfs_tx_sequence = 0;
    qfs_mountpoint = 0;
}
#endif

const struct vfs_filesystem *qfs_filesystem(void)
{
    return &qfs_descriptor;
}
