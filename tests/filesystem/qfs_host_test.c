#include <stdio.h>
#include <string.h>

#include "include/qmjy/block.h"
#include "fs/vfs/vfs.h"
#include "fs/qfs/qfs.h"

#define TEST_DEVICE_ID          0x51465301ULL
#define TEST_SECTOR_SIZE        512U
#define TEST_DISK_SECTORS       4096U
#define TEST_PARTITION_START    1U
#define TEST_PARTITION_SECTORS  2048U

static const char first_data[] = "qmjyOS QFS persistent data";
static const char tail_data[] = "QFS-TAIL";

#define TEST_FILE_LENGTH (1024U + sizeof(tail_data))
#define TEST_GUARD_SIZE  16U
#define TEST_GUARD_VALUE 0xD7U

static unsigned char test_disk[
    TEST_DISK_SECTORS * TEST_SECTOR_SIZE
];

struct guarded_buffer {
    unsigned char before[TEST_GUARD_SIZE];
    unsigned char data[TEST_FILE_LENGTH];
    unsigned char after[TEST_GUARD_SIZE];
};

static qm_status_t test_disk_read(
    qm_block_device_id_t id,
    qm_u64_t lba,
    qm_u32_t count,
    void *buffer)
{
    if (id != TEST_DEVICE_ID || buffer == NULL ||
        count == 0 ||
        lba >= TEST_DISK_SECTORS ||
        count > TEST_DISK_SECTORS - lba)
        return QM_ERR_RANGE;

    memcpy(
        buffer,
        &test_disk[lba * TEST_SECTOR_SIZE],
        count * TEST_SECTOR_SIZE);

    return QM_OK;
}

static qm_status_t test_disk_write(
    qm_block_device_id_t id,
    qm_u64_t lba,
    qm_u32_t count,
    const void *buffer)
{
    if (id != TEST_DEVICE_ID || buffer == NULL ||
        count == 0 ||
        lba >= TEST_DISK_SECTORS ||
        count > TEST_DISK_SECTORS - lba)
        return QM_ERR_RANGE;

    memcpy(
        &test_disk[lba * TEST_SECTOR_SIZE],
        buffer,
        count * TEST_SECTOR_SIZE);

    return QM_OK;
}

static qm_status_t test_disk_flush(qm_block_device_id_t id)
{
    return id == TEST_DEVICE_ID ? QM_OK : QM_ERR_NOT_FOUND;
}

static const qm_block_device_ops_t test_disk_ops = {
    .read = test_disk_read,
    .write = test_disk_write,
    .flush = test_disk_flush
};

#define CHECK(condition, message)                  \
    do {                                            \
        if (!(condition)) {                         \
            printf("[FAIL] %s\n", message);         \
            return 1;                               \
        }                                           \
        printf("[PASS] %s\n", message);             \
    } while (0)

static void guarded_buffer_init(struct guarded_buffer *buffer)
{
    memset(buffer->before, TEST_GUARD_VALUE,
           sizeof(buffer->before));

    memset(buffer->data, 0xAA, sizeof(buffer->data));

    memset(buffer->after, TEST_GUARD_VALUE,
           sizeof(buffer->after));
}

static int guarded_buffer_valid(const struct guarded_buffer *buffer)
{
    unsigned int i;

    for (i = 0; i < TEST_GUARD_SIZE; ++i) {
        if (buffer->before[i] != TEST_GUARD_VALUE ||
            buffer->after[i] != TEST_GUARD_VALUE)
            return 0;
    }

    return 1;
}

int main(void)
{
    qm_block_device_info_t device_info = {
        .id = TEST_DEVICE_ID,
        .block_count = TEST_DISK_SECTORS,
        .block_size = TEST_SECTOR_SIZE,
        .flags = 0,
        .name = "qfs-test-disk"
    };

    qm_partition_t partition;

    struct vfs_node *root;
    struct vfs_node *file = NULL;
    struct vfs_node *found = NULL;

    struct guarded_buffer readback;

    unsigned int i;
    int gap_is_zero;

    memset(test_disk, 0, sizeof(test_disk));
    memset(&partition, 0, sizeof(partition));

    partition.device_id = TEST_DEVICE_ID;
    partition.index = 0;
    partition.type = 0x83;
    partition.bootable = 0;
    partition.start_lba = TEST_PARTITION_START;
    partition.sector_count = TEST_PARTITION_SECTORS;

    CHECK(
        qm_block_device_register(
            &device_info,
            &test_disk_ops) == QM_OK,
        "register memory-backed block device");

    CHECK(
        qfs_format(&partition) == VFS_OK,
        "format QFS partition");

    CHECK(
        qfs_mount(&partition) == VFS_OK,
        "mount formatted QFS");

    root = qfs_root();

    CHECK(
        root != NULL,
        "obtain QFS root");

    CHECK(
        vfs_init(root) == VFS_OK,
        "initialize VFS with QFS root");

    CHECK(
        vfs_register_filesystem(qfs_filesystem()) == VFS_OK,
        "register QFS filesystem");

    CHECK(
        vfs_create(
            root,
            "persist.txt",
            VFS_NODE_FILE,
            &file) == VFS_OK,
        "create persistent file");

    CHECK(
        file != NULL,
        "obtain created file node");

    CHECK(
        vfs_write(
            file,
            0,
            first_data,
            sizeof(first_data)) == (int)sizeof(first_data),
        "write initial file contents");

    CHECK(
        vfs_write(
            file,
            1024,
            tail_data,
            sizeof(tail_data)) == (int)sizeof(tail_data),
        "write data beyond the first two sectors");

    guarded_buffer_init(&readback);

    CHECK(
        vfs_read(
            file,
            0,
            readback.data,
            TEST_FILE_LENGTH) == (int)TEST_FILE_LENGTH,
        "read file including sparse gap");

    CHECK(
        guarded_buffer_valid(&readback),
        "verify read did not cross buffer boundaries");

    CHECK(
        memcmp(
            readback.data,
            first_data,
            sizeof(first_data)) == 0,
        "verify initial data");

    gap_is_zero = 1;

    for (i = sizeof(first_data); i < 1024U; ++i) {
        if (readback.data[i] != 0) {
            gap_is_zero = 0;
            break;
        }
    }

    CHECK(
        gap_is_zero,
        "verify sparse gap is zero-filled");

    CHECK(
        memcmp(
            readback.data + 1024U,
            tail_data,
            sizeof(tail_data)) == 0,
        "verify data in the third sector");

    CHECK(
        qfs_unmount() == VFS_OK,
        "unmount QFS");

    CHECK(
        qfs_mount(&partition) == VFS_OK,
        "remount QFS from the same disk image");

    root = qfs_root();

    CHECK(
        root != NULL,
        "obtain QFS root after remount");

    CHECK(
        vfs_init(root) == VFS_OK,
        "reinitialize VFS after remount");

    CHECK(
        vfs_register_filesystem(qfs_filesystem()) == VFS_OK,
        "register QFS after remount");

    CHECK(
        vfs_lookup("/persist.txt", &found) == VFS_OK,
        "find file after remount");

    CHECK(
        found != NULL,
        "obtain persistent file node");

    guarded_buffer_init(&readback);

    CHECK(
        vfs_read(
            found,
            0,
            readback.data,
            TEST_FILE_LENGTH) == (int)TEST_FILE_LENGTH,
        "read persistent file after remount");

    CHECK(
        guarded_buffer_valid(&readback),
        "verify remount read did not cross buffer boundaries");

    CHECK(
        memcmp(
            readback.data,
            first_data,
            sizeof(first_data)) == 0 &&
        memcmp(
            readback.data + 1024U,
            tail_data,
            sizeof(tail_data)) == 0,
        "verify contents survived unmount and remount");

    gap_is_zero = 1;

    for (i = sizeof(first_data); i < 1024U; ++i) {
        if (readback.data[i] != 0) {
            gap_is_zero = 0;
            break;
        }
    }

    CHECK(
        gap_is_zero,
        "verify sparse gap survived remount");

    CHECK(
        qfs_unmount() == VFS_OK,
        "final QFS unmount");

    CHECK(
        qm_block_device_unregister(TEST_DEVICE_ID) == QM_OK,
        "unregister test block device");

    printf("\nQFS PERSISTENCE TESTS PASSED\n");

    return 0;
}
