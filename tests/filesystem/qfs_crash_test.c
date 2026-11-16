#include <stdio.h>
#include <string.h>
#include "include/qmjy/block.h"
#include "fs/vfs/vfs.h"
#include "fs/qfs/qfs.h"

#define DEV_ID 0x51465302ULL
#define SECTOR_SIZE 512U
#define DISK_SECTORS 512U
#define PART_START 1U
#define PART_SECTORS 256U
#define DISK_BYTES ((size_t)DISK_SECTORS * SECTOR_SIZE)

static unsigned char durable_disk[DISK_BYTES];
static unsigned char volatile_disk[DISK_BYTES];
static qm_partition_t partition;
static unsigned long io_count;
static unsigned long fail_at;

static int inject_failure(void)
{
    ++io_count;
    return fail_at != 0 && io_count == fail_at;
}

static qm_status_t disk_read(qm_block_device_id_t id, qm_u64_t lba,
                             qm_u32_t count, void *buffer)
{
    if (id != DEV_ID || buffer == 0 || count == 0 ||
        lba >= DISK_SECTORS || count > DISK_SECTORS - lba)
        return QM_ERR_RANGE;
    memcpy(buffer, volatile_disk + (size_t)lba * SECTOR_SIZE,
           (size_t)count * SECTOR_SIZE);
    return QM_OK;
}

static qm_status_t disk_write(qm_block_device_id_t id, qm_u64_t lba,
                              qm_u32_t count, const void *buffer)
{
    if (id != DEV_ID || buffer == 0 || count == 0 ||
        lba >= DISK_SECTORS || count > DISK_SECTORS - lba)
        return QM_ERR_RANGE;
    memcpy(volatile_disk + (size_t)lba * SECTOR_SIZE, buffer,
           (size_t)count * SECTOR_SIZE);
    /* Report an error after the volatile write to model uncertain completion. */
    return inject_failure() ? QM_ERR_IO : QM_OK;
}

static qm_status_t disk_flush(qm_block_device_id_t id)
{
    if (id != DEV_ID)
        return QM_ERR_NOT_FOUND;
    memcpy(durable_disk, volatile_disk, DISK_BYTES);
    /* A failed flush may nevertheless have reached stable storage. */
    return inject_failure() ? QM_ERR_IO : QM_OK;
}

static const qm_block_device_ops_t disk_ops = {
    .read = disk_read,
    .write = disk_write,
    .flush = disk_flush
};

#define NOFLUSH_DEV_ID (DEV_ID + 1U)
static qm_status_t noflush_read(qm_block_device_id_t id, qm_u64_t lba,
                                qm_u32_t count, void *buffer)
{
    (void)lba; (void)count; (void)buffer;
    return id == NOFLUSH_DEV_ID ? QM_OK : QM_ERR_NOT_FOUND;
}
static const qm_block_device_ops_t noflush_ops = {
    .read = noflush_read,
    .write = 0,
    .flush = 0
};

#define REQUIRE(c, m) do { if (!(c)) { \
    printf("[QFS CRASH FAIL] %s (line %d)\n", m, __LINE__); return 1; \
} } while (0)

static void simulate_power_loss(void)
{
    memcpy(volatile_disk, durable_disk, DISK_BYTES);
    fail_at = 0;
    io_count = 0;
}

static int mount_after_crash(struct vfs_node **root_out)
{
    int status;
    qfs_test_reset_state();
    fail_at = 0;
    io_count = 0;
    status = qfs_mount(&partition);
    if (status != VFS_OK)
        return status;
    *root_out = qfs_root();
    if (*root_out == 0 || vfs_init(*root_out) != VFS_OK)
        return VFS_ERR_IO;
    return VFS_OK;
}

static int baseline_create(void)
{
    struct vfs_node *root;
    struct vfs_node *file = 0;
    static const char old_text[] = "old-stable-qfs-content";

    memset(durable_disk, 0, DISK_BYTES);
    memset(volatile_disk, 0, DISK_BYTES);
    simulate_power_loss();
    qfs_test_reset_state();
    if (qfs_format(&partition) != VFS_OK ||
        qfs_mount(&partition) != VFS_OK)
        return -1;
    root = qfs_root();
    if (root == 0 || vfs_init(root) != VFS_OK ||
        vfs_create(root, "base", VFS_NODE_FILE, &file) != VFS_OK ||
        file == 0 ||
        vfs_write(file, 0, old_text, sizeof(old_text)) !=
            (int)sizeof(old_text) ||
        qfs_unmount() != VFS_OK)
        return -1;
    return 0;
}

enum operation { OP_CREATE, OP_WRITE, OP_REMOVE, OP_FORMAT };

static int perform_mounted_operation(enum operation op)
{
    struct vfs_node *root = qfs_root();
    struct vfs_node *node = 0;
    static char replacement[4096];
    unsigned int i;

    if (root == 0)
        return VFS_ERR_IO;
    if (op == OP_CREATE)
        return vfs_create(root, "new-file", VFS_NODE_FILE, &node);
    if (vfs_lookup("/base", &node) != VFS_OK || node == 0)
        return VFS_ERR_NOT_FOUND;
    if (op == OP_WRITE) {
        for (i = 0; i < sizeof(replacement); ++i)
            replacement[i] = 'N';
        return vfs_write(node, 0, replacement, sizeof(replacement));
    }
    return vfs_remove(root, node);
}

static int verify_operation(enum operation op)
{
    struct vfs_node *root = 0;
    struct vfs_node *node = 0;
    char data[4096];
    static const char old_text[] = "old-stable-qfs-content";
    int got;
    unsigned int i;
    int mount_status = mount_after_crash(&root);

    if (op == OP_FORMAT && mount_status != VFS_OK) {
        REQUIRE(mount_status == VFS_ERR_INVALID || mount_status == VFS_ERR_IO,
                "interrupted format may only leave a safely rejected volume");
        return 0;
    }
    REQUIRE(mount_status == VFS_OK,
            "remount must recover to a valid old or new state");

    if (op == OP_CREATE) {
        int status = vfs_lookup("/new-file", &node);
        REQUIRE(status == VFS_OK || status == VFS_ERR_NOT_FOUND,
                "create must be wholly present or absent");
        if (status == VFS_OK)
            REQUIRE(node != 0 && node->size == 0,
                    "created file metadata must be complete");
    } else if (op == OP_REMOVE) {
        int status = vfs_lookup("/base", &node);
        REQUIRE(status == VFS_OK || status == VFS_ERR_NOT_FOUND,
                "remove must be wholly present or absent");
        if (status == VFS_OK) {
            got = vfs_read(node, 0, data, sizeof(data));
            REQUIRE(got == (int)sizeof(old_text),
                    "a still-present file must retain its old size");
            REQUIRE(memcmp(data, old_text, sizeof(old_text)) == 0,
                    "a still-present file must retain old contents");
        }
    } else if (op == OP_WRITE) {
        REQUIRE(vfs_lookup("/base", &node) == VFS_OK && node != 0,
                "write target must remain present");
        got = vfs_read(node, 0, data, sizeof(data));
        REQUIRE(got == (int)sizeof(old_text) || got == (int)sizeof(data),
                "write must expose either the entire old or new length");
        if (got == (int)sizeof(old_text)) {
            REQUIRE(memcmp(data, old_text, sizeof(old_text)) == 0,
                    "uncommitted write must preserve old data");
        } else {
            for (i = 0; i < sizeof(data); ++i)
                REQUIRE(data[i] == 'N',
                        "committed write must replay every data sector");
        }
    } else {
        int status = vfs_lookup("/base", &node);
        REQUIRE(status == VFS_OK || status == VFS_ERR_NOT_FOUND,
                "formatted volume must be complete old or empty new state");
        if (status == VFS_OK && node != 0) {
            got = vfs_read(node, 0, data, sizeof(data));
            REQUIRE(got == (int)sizeof(old_text),
                    "old file must remain complete if old superblock survived");
            REQUIRE(memcmp(data, old_text, sizeof(old_text)) == 0,
                    "old file contents must remain intact");
        }
    }
    REQUIRE(qfs_unmount() == VFS_OK, "unmount after recovery");
    return 0;
}

static int exercise_failpoints(enum operation op, const char *label)
{
    unsigned char baseline[DISK_BYTES];
    unsigned long total;
    unsigned long point;
    struct vfs_node *root = 0;
    int status;

    REQUIRE(baseline_create() == 0, "prepare durable baseline image");
    memcpy(baseline, durable_disk, DISK_BYTES);

    /* Count callbacks belonging to the operation, excluding mount I/O. */
    memcpy(durable_disk, baseline, DISK_BYTES);
    memcpy(volatile_disk, baseline, DISK_BYTES);
    qfs_test_reset_state();
    fail_at = 0;
    io_count = 0;
    if (op == OP_FORMAT) {
        status = qfs_format(&partition);
    } else {
        REQUIRE(qfs_mount(&partition) == VFS_OK,
                "baseline must mount before operation count");
        root = qfs_root();
        REQUIRE(root != 0 && vfs_init(root) == VFS_OK,
                "initialize VFS before operation count");
        io_count = 0;
        status = perform_mounted_operation(op);
    }
    if (op == OP_WRITE)
        REQUIRE(status == 4096, "unfaulted full-file write succeeds");
    else
        REQUIRE(status == VFS_OK, "unfaulted operation succeeds");
    total = io_count;
    REQUIRE(total > 0, "operation must exercise persistence callbacks");

    for (point = 1; point <= total; ++point) {
        memcpy(durable_disk, baseline, DISK_BYTES);
        memcpy(volatile_disk, baseline, DISK_BYTES);
        qfs_test_reset_state();
        fail_at = 0;
        io_count = 0;
        if (op == OP_FORMAT) {
            fail_at = point;
            status = qfs_format(&partition);
        } else {
            REQUIRE(qfs_mount(&partition) == VFS_OK,
                    "baseline must mount before injected operation");
            root = qfs_root();
            REQUIRE(root != 0 && vfs_init(root) == VFS_OK,
                    "initialize VFS before injected operation");
            fail_at = point;
            io_count = 0;
            status = perform_mounted_operation(op);
        }
        if (op == OP_WRITE)
            REQUIRE(status != 4096,
                    "injected persistence fault must fail the write call");
        else
            REQUIRE(status != VFS_OK,
                    "injected persistence fault must fail the operation call");
        simulate_power_loss();
        REQUIRE(verify_operation(op) == 0,
                "every write/flush failure point must recover atomically");
    }

    printf("[QFS CRASH PASS] %s: %lu injected persistence boundaries\n",
           label, total);
    return 0;
}

static int verify_committed_write(void)
{
    struct vfs_node *root = 0;
    struct vfs_node *node = 0;
    static char expected[4096];
    char actual[4096];
    unsigned int i;

    for (i = 0; i < sizeof(expected); ++i)
        expected[i] = 'N';
    REQUIRE(mount_after_crash(&root) == VFS_OK,
            "committed journal must recover after a second recovery crash");
    REQUIRE(vfs_lookup("/base", &node) == VFS_OK && node != 0,
            "recovered committed file remains present");
    REQUIRE(vfs_read(node, 0, actual, sizeof(actual)) == (int)sizeof(actual),
            "recovered committed file has complete size");
    REQUIRE(memcmp(actual, expected, sizeof(expected)) == 0,
            "recovered committed file contains all after-image sectors");
    REQUIRE(qfs_unmount() == VFS_OK,
            "unmount after repeated journal recovery");
    return 0;
}

static int exercise_recovery_failpoints(void)
{
    unsigned char pending[DISK_BYTES];
    struct vfs_node *root = 0;
    unsigned long recovery_total;
    unsigned long point;
    int status;

    REQUIRE(baseline_create() == 0,
            "prepare baseline before committed recovery test");
    qfs_test_reset_state();
    REQUIRE(qfs_mount(&partition) == VFS_OK,
            "mount baseline before creating committed journal");
    root = qfs_root();
    REQUIRE(root != 0 && vfs_init(root) == VFS_OK,
            "initialize VFS before creating committed journal");

    /* For a 4 KiB rewrite: 10 payloads + header + flush + 2 markers + flush.
       Fail the first home-sector write after the commit markers are durable. */
    fail_at = 16;
    io_count = 0;
    status = perform_mounted_operation(OP_WRITE);
    REQUIRE(status == VFS_ERR_IO,
            "injected first home write must leave a committed redo log");
    simulate_power_loss();
    memcpy(pending, durable_disk, DISK_BYTES);

    /* Measure the actual recovery callback count once without a fault. */
    memcpy(durable_disk, pending, DISK_BYTES);
    memcpy(volatile_disk, pending, DISK_BYTES);
    qfs_test_reset_state();
    fail_at = 0;
    io_count = 0;
    REQUIRE(qfs_mount(&partition) == VFS_OK,
            "unfaulted recovery of committed redo log succeeds");
    recovery_total = io_count;
    REQUIRE(recovery_total > 0,
            "recovery must exercise persistence callbacks");
    REQUIRE(qfs_unmount() == VFS_OK,
            "unmount after measuring recovery callbacks");

    for (point = 1; point <= recovery_total; ++point) {
        memcpy(durable_disk, pending, DISK_BYTES);
        memcpy(volatile_disk, pending, DISK_BYTES);
        qfs_test_reset_state();
        fail_at = point;
        io_count = 0;
        status = qfs_mount(&partition);
        REQUIRE(status == VFS_ERR_IO,
                "each injected recovery write/flush failure is reported");
        simulate_power_loss();
        REQUIRE(verify_committed_write() == 0,
                "recovery must be idempotent after another power loss");
    }

    printf("[QFS CRASH PASS] recovery replay: %lu injected persistence boundaries\n",
           recovery_total);
    return 0;
}

static int test_missing_flush_is_not_success(void)
{
    qm_block_device_info_t info;
    memset(&info, 0, sizeof(info));
    info.id = NOFLUSH_DEV_ID;
    qm_partition_t noflush_partition;
    info.block_count = 128;
    info.block_size = SECTOR_SIZE;
    info.name = "qfs-no-flush-test";
    memset(&noflush_partition, 0, sizeof(noflush_partition));
    noflush_partition.device_id = NOFLUSH_DEV_ID;
    noflush_partition.sector_count = 64;
    REQUIRE(qm_block_device_register(&info, &noflush_ops) == QM_OK,
            "register device without flush callback");
    REQUIRE(qm_block_flush(NOFLUSH_DEV_ID) == QM_ERR_NOT_SUPPORTED,
            "missing flush callback must not report durable success");
    REQUIRE(qfs_format(&noflush_partition) == VFS_ERR_IO,
            "QFS v2 format must reject a device without flush support");
    qfs_test_reset_state();
    REQUIRE(qm_block_device_unregister(NOFLUSH_DEV_ID) == QM_OK,
            "unregister no-flush test device");
    printf("[QFS CRASH PASS] missing flush callback is rejected\n");
    return 0;
}

static int test_legacy_v1_read_only(void)
{
    unsigned char sb[SECTOR_SIZE];
    unsigned char bitmap[SECTOR_SIZE];
    unsigned char inode_sector[SECTOR_SIZE];
    unsigned char directory_sector[SECTOR_SIZE];
    static const char legacy_data[] = "legacy-v1-readable-data";
    char readback[sizeof(legacy_data)];
    struct vfs_node *root;
    struct vfs_node *node = 0;
    unsigned int i;

    memset(durable_disk, 0, DISK_BYTES);
    memset(volatile_disk, 0, DISK_BYTES);
    memset(sb, 0, sizeof(sb));
    memset(bitmap, 0, sizeof(bitmap));
    memset(inode_sector, 0, sizeof(inode_sector));
    memset(directory_sector, 0, sizeof(directory_sector));
    qfs_test_reset_state();

    /* Build a valid QFS v1 image with one real file in data sector 25. */
    sb[0] = 0x51; sb[1] = 0x46; sb[2] = 0x53; sb[3] = 0x31;
    sb[4] = 1;
    sb[8] = (unsigned char)(PART_SECTORS & 0xFFU);
    sb[9] = (unsigned char)((PART_SECTORS >> 8) & 0xFFU);
    sb[12] = 25;
    sb[16] = 64;
    sb[20] = 0x00; sb[21] = 0x10;
    sb[24] = 1; sb[28] = 9; sb[32] = 17;
    for (i = 0; i <= 25; ++i)
        bitmap[i / 8U] |= (unsigned char)(1U << (i % 8U));

    inode_sector[64] = 1; /* inode 1: used */
    inode_sector[65] = 1; /* regular file */
    inode_sector[68] = (unsigned char)(sizeof(legacy_data) & 0xFFU);
    inode_sector[69] = (unsigned char)((sizeof(legacy_data) >> 8) & 0xFFU);
    inode_sector[72] = 25; /* first direct block */

    directory_sector[0] = 1; /* directory entry references inode 1 */
    memcpy(&directory_sector[4], "legacy.txt", sizeof("legacy.txt"));

    memcpy(volatile_disk + (size_t)PART_START * SECTOR_SIZE,
           sb, SECTOR_SIZE);
    memcpy(volatile_disk + (size_t)(PART_START + 1U) * SECTOR_SIZE,
           bitmap, SECTOR_SIZE);
    memcpy(volatile_disk + (size_t)(PART_START + 9U) * SECTOR_SIZE,
           inode_sector, SECTOR_SIZE);
    memcpy(volatile_disk + (size_t)(PART_START + 17U) * SECTOR_SIZE,
           directory_sector, SECTOR_SIZE);
    memcpy(volatile_disk + (size_t)(PART_START + 25U) * SECTOR_SIZE,
           legacy_data, sizeof(legacy_data));
    memcpy(durable_disk, volatile_disk, DISK_BYTES);

    REQUIRE(qfs_mount(&partition) == VFS_OK,
            "legacy QFS v1 image remains mountable");
    root = qfs_root();
    REQUIRE(root != 0 && vfs_init(root) == VFS_OK,
            "initialize read-only QFS v1 root");
    REQUIRE(vfs_lookup("/legacy.txt", &node) == VFS_OK && node != 0,
            "legacy QFS v1 file remains readable by name");
    REQUIRE(vfs_read(node, 0, readback, sizeof(readback)) ==
                (int)sizeof(readback),
            "read legacy QFS v1 file contents");
    REQUIRE(memcmp(readback, legacy_data, sizeof(legacy_data)) == 0,
            "legacy QFS v1 file contents are preserved");
    REQUIRE(vfs_create(root, "forbidden", VFS_NODE_FILE, &node) ==
                VFS_ERR_NOT_SUPPORTED,
            "legacy QFS v1 must not accept unsafe mutations");
    REQUIRE(qfs_unmount() == VFS_OK,
            "legacy read-only unmount does not require a flush callback");
    printf("[QFS CRASH PASS] QFS v1 data remains readable and read-only\\n");
    return 0;
}

int main(void)
{
    qm_block_device_info_t info;
    memset(&info, 0, sizeof(info));
    info.id = DEV_ID;
    info.block_count = DISK_SECTORS;
    info.block_size = SECTOR_SIZE;
    info.name = "qfs-crash-test";
    partition.device_id = DEV_ID;
    partition.index = 0;
    partition.type = 0x83;
    partition.start_lba = PART_START;
    partition.sector_count = PART_SECTORS;

    REQUIRE(qm_block_device_register(&info, &disk_ops) == QM_OK,
            "register volatile/durable test disk");
    REQUIRE(test_missing_flush_is_not_success() == 0,
            "missing flush contract test");
    REQUIRE(test_legacy_v1_read_only() == 0,
            "legacy compatibility test");
    REQUIRE(exercise_failpoints(OP_CREATE, "create") == 0,
            "create failpoint matrix");
    REQUIRE(exercise_failpoints(OP_WRITE, "write") == 0,
            "write failpoint matrix");
    REQUIRE(exercise_failpoints(OP_REMOVE, "remove") == 0,
            "remove failpoint matrix");
    REQUIRE(exercise_failpoints(OP_FORMAT, "format") == 0,
            "format failpoint matrix");
    REQUIRE(exercise_recovery_failpoints() == 0,
            "recovery replay failpoint matrix");

    printf("QFS CRASH-CONSISTENCY TESTS PASSED\n");
    return 0;
}
