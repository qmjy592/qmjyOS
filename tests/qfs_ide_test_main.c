#include <stddef.h>
#include <stdint.h>

#include "serial.h"
#include "../fs/qfs/qfs.h"
#include "../fs/vfs/vfs.h"
#include "../fs/partition/mbr.h"
#include "../drivers/block/ide_pio.h"

#if !defined(QFS_DISK_TEST_MODE)
#error "QFS_DISK_TEST_MODE must be defined"
#endif

#if QFS_DISK_TEST_MODE != 1 && QFS_DISK_TEST_MODE != 2
#error "QFS_DISK_TEST_MODE must be 1 or 2"
#endif

#define QFS_TEST_PARTITION_TYPE  0x7FU
#define QFS_TEST_PARTITION_START 2048ULL
#define QFS_TEST_PARTITION_SIZE  2048ULL

static const char test_payload[] =
    "qmjyOS-QFS-IDE-PERSISTENCE-v1: "
    "format-once, verify-after-reboot\n";

void ring3_syscall_handler(uint64_t syscall_number)
{
    (void)syscall_number;
}

/*
 * QEMU debugcon: port 0xE9.
 * This path deliberately does not depend on serial_init().
 */
static void qfs_debug_putc(char c)
{
    __asm__ volatile(
        "outb %0, %1"
        :
        : "a"((uint8_t)c), "Nd"((uint16_t)0xE9)
    );
}

static void qfs_debug_puts(const char *s)
{
    if (s == NULL)
        return;

    while (*s)
        qfs_debug_putc(*s++);
}

static void qfs_log(const char *message)
{
    qfs_debug_puts(message);
    serial_puts(message);
}

static void qfs_test_halt(void)
{
    for (;;) {
        __asm__ volatile("cli; hlt" ::: "memory");
    }
}

static void qfs_test_fail(const char *message)
{
    qfs_debug_puts("[QFS FAIL] ");
    qfs_debug_puts(message);
    qfs_debug_puts("\n");

    serial_puts("[QFS IDE TEST FAIL] ");
    serial_puts(message);
    serial_puts("\n");

    qfs_test_halt();
}

static int qfs_test_equal(
    const char *left,
    const char *right,
    size_t length)
{
    size_t i;

    for (i = 0; i < length; ++i) {
        if (left[i] != right[i])
            return 0;
    }

    return 1;
}

void main(uint32_t multiboot_magic, uint32_t multiboot_info_addr)
{
    qm_partition_t partitions[QM_MBR_PARTITION_COUNT];
    qm_u32_t partition_count = 0;
    struct vfs_node *root;
    struct vfs_node *file = NULL;
    char readback[sizeof(test_payload)];

    (void)multiboot_magic;
    (void)multiboot_info_addr;

    qfs_debug_puts("[QFS] MAIN_ENTER\n");

    serial_init();
    qfs_log("[QFS] SERIAL_READY\n");

    qfs_log("[QFS] IDE_INIT_BEGIN\n");
    if (qm_ide_pio_init() != 0)
        qfs_test_fail("IDE controller initialization failed");
    qfs_log("[QFS] IDE_INIT_OK\n");

    qfs_log("[QFS] MBR_SCAN_BEGIN\n");
    if (qm_mbr_scan(
            QM_IDE_PRIMARY_SLAVE,
            partitions,
            QM_MBR_PARTITION_COUNT,
            &partition_count) != 0) {
        qfs_test_fail("secondary IDE MBR scan failed");
    }
    qfs_log("[QFS] MBR_SCAN_OK\n");

    if (partition_count != 1)
        qfs_test_fail("expected exactly one test partition");

    if (partitions[0].device_id != QM_IDE_PRIMARY_SLAVE ||
        partitions[0].type != QFS_TEST_PARTITION_TYPE ||
        partitions[0].start_lba != QFS_TEST_PARTITION_START ||
        partitions[0].sector_count != QFS_TEST_PARTITION_SIZE) {
        qfs_test_fail("test partition identity or bounds mismatch");
    }

    qfs_log("[QFS] PARTITION_CHECK_OK\n");

#if QFS_DISK_TEST_MODE == 1
    qfs_log("[QFS] FORMAT_BEGIN\n");

    if (qfs_format(&partitions[0]) != VFS_OK)
        qfs_test_fail("qfs_format failed");

    qfs_log("[QFS] FORMAT_OK\n");
#endif

    qfs_log("[QFS] MOUNT_BEGIN\n");
    if (qfs_mount(&partitions[0]) != VFS_OK)
        qfs_test_fail("QFS mount failed");
    qfs_log("[QFS] MOUNT_OK\n");

    root = qfs_root();
    if (root == NULL)
        qfs_test_fail("QFS root is unavailable");

    qfs_log("[QFS] VFS_INIT_BEGIN\n");
    if (vfs_init(root) != VFS_OK)
        qfs_test_fail("VFS initialization failed");
    qfs_log("[QFS] VFS_INIT_OK\n");

#if QFS_DISK_TEST_MODE == 1
    qfs_log("[QFS] CREATE_BEGIN\n");

    if (vfs_create(
            root,
            "qfs-persist.txt",
            VFS_NODE_FILE,
            &file) != VFS_OK) {
        qfs_test_fail("persistent test file creation failed");
    }
    qfs_log("[QFS] CREATE_OK\n");

    qfs_log("[QFS] WRITE_BEGIN\n");

    if (vfs_write(
            file,
            0,
            test_payload,
            sizeof(test_payload)) != (int)sizeof(test_payload)) {
        qfs_test_fail("persistent test file write failed");
    }
    qfs_log("[QFS] WRITE_OK\n");

    qfs_log("[QFS] READBACK_BEGIN\n");

    if (vfs_read(
            file,
            0,
            readback,
            sizeof(readback)) != (int)sizeof(readback)) {
        qfs_test_fail("immediate readback failed");
    }

    if (!qfs_test_equal(
            readback,
            test_payload,
            sizeof(test_payload))) {
        qfs_test_fail("immediate readback content mismatch");
    }
    qfs_log("[QFS] READBACK_OK\n");

    qfs_log("[QFS] UNMOUNT_BEGIN\n");

    if (qfs_unmount() != VFS_OK)
        qfs_test_fail("QFS flush or unmount failed");

    qfs_log("[QFS] UNMOUNT_OK\n");

    qfs_log(
        "[QFS IDE FORMAT PASS] Formatted, wrote, read and flushed.\n"
    );

#elif QFS_DISK_TEST_MODE == 2
    qfs_log("[QFS] LOOKUP_BEGIN\n");

    if (vfs_lookup("/qfs-persist.txt", &file) != VFS_OK ||
        file == NULL) {
        qfs_test_fail("persistent file not found after reboot");
    }
    qfs_log("[QFS] LOOKUP_OK\n");

    qfs_log("[QFS] PERSIST_READ_BEGIN\n");

    if (vfs_read(
            file,
            0,
            readback,
            sizeof(readback)) != (int)sizeof(readback)) {
        qfs_test_fail("persistent file read failed after reboot");
    }

    if (!qfs_test_equal(
            readback,
            test_payload,
            sizeof(test_payload))) {
        qfs_test_fail("persistent file content mismatch after reboot");
    }
    qfs_log("[QFS] PERSIST_READ_OK\n");

    qfs_log(
        "[QFS IDE VERIFY PASS] Data survived independent QEMU restart.\n"
    );
#endif

    qfs_log("[QFS] TEST_COMPLETE\n");
    qfs_test_halt();
}
