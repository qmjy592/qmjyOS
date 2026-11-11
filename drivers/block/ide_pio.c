#include "ide_pio.h"

/* Primary IDE channel ports. */
#define IDE_DATA        0x1F0
#define IDE_ERROR       0x1F1
#define IDE_FEATURES    0x1F1
#define IDE_SECCOUNT0   0x1F2
#define IDE_LBA0        0x1F3
#define IDE_LBA1        0x1F4
#define IDE_LBA2        0x1F5
#define IDE_HDDEVSEL    0x1F6
#define IDE_COMMAND     0x1F7
#define IDE_STATUS      0x1F7
#define IDE_ALTSTATUS   0x3F6
#define IDE_CONTROL     0x3F6

#define IDE_STATUS_ERR  0x01
#define IDE_STATUS_DRQ  0x08
#define IDE_STATUS_DF   0x20
#define IDE_STATUS_BSY  0x80

#define IDE_CMD_READ       0x20
#define IDE_CMD_WRITE      0x30
#define IDE_CMD_IDENTIFY   0xEC
#define IDE_CMD_FLUSH      0xE7

#define IDE_SECTOR_SIZE 512
#define IDE_WORDS_PER_SECTOR 256
#define IDE_TIMEOUT 1000000U
#define IDE_MAX_LBA28 0x0FFFFFFFULL

#define IDE_DEVICE_MASTER 0
#define IDE_DEVICE_SLAVE  1

struct ide_device_info {
    qm_u32_t sectors;
    qm_u8_t present;
    qm_u8_t slave;
};

static struct ide_device_info ide_devices[2];

static inline void ide_outb(qm_u16_t port, qm_u8_t value)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(value), "Nd"(port)
    );
}

static inline qm_u8_t ide_inb(qm_u16_t port)
{
    qm_u8_t value;

    __asm__ volatile (
        "inb %1, %0"
        : "=a"(value)
        : "Nd"(port)
    );

    return value;
}

static inline void ide_io_delay(void)
{
    (void)ide_inb(IDE_ALTSTATUS);
    (void)ide_inb(IDE_ALTSTATUS);
    (void)ide_inb(IDE_ALTSTATUS);
    (void)ide_inb(IDE_ALTSTATUS);
}

static inline void ide_insw(
    qm_u16_t port,
    void *buffer,
    qm_u32_t words)
{
    __asm__ volatile (
        "cld\n"
        "rep insw"
        : "+D"(buffer), "+c"(words)
        : "d"(port)
        : "memory"
    );
}

static inline void ide_outsw(
    qm_u16_t port,
    const void *buffer,
    qm_u32_t words)
{
    const void *source = buffer;

    __asm__ volatile (
        "cld\n"
        "rep outsw"
        : "+S"(source), "+c"(words)
        : "d"(port)
        : "memory"
    );
}

/*
 * Wait until BSY clears.
 * If require_drq is nonzero, also require DRQ.
 */
static int ide_wait(int require_drq)
{
    qm_u32_t timeout;
    qm_u8_t status;

    for (timeout = 0; timeout < IDE_TIMEOUT; ++timeout) {
        status = ide_inb(IDE_STATUS);

        if (status == 0xFF || status == 0x00)
            return -1;

        if (status & IDE_STATUS_BSY)
            continue;

        if (status & (IDE_STATUS_ERR | IDE_STATUS_DF))
            return -1;

        if (require_drq) {
            if (status & IDE_STATUS_DRQ)
                return 0;
        } else {
            return 0;
        }
    }

    return -1;
}


/*
 * Wait for an ATA command to complete.
 * Completion requires BSY=0, DRQ=0 and no device error.
 */
static int ide_wait_complete(void)
{
    qm_u32_t timeout;
    qm_u8_t status;

    for (timeout = 0; timeout < IDE_TIMEOUT; ++timeout) {
        status = ide_inb(IDE_STATUS);

        if (status == 0xFF || status == 0x00)
            return -1;

        if (status & IDE_STATUS_BSY)
            continue;

        if (status & (IDE_STATUS_ERR | IDE_STATUS_DF))
            return -1;

        if (!(status & IDE_STATUS_DRQ))
            return 0;
    }

    return -1;
}

static void ide_select(qm_u8_t slave)
{
    ide_outb(
        IDE_HDDEVSEL,
        (qm_u8_t)(0xA0 | (slave << 4))
    );

    ide_io_delay();
}

static int ide_identify(
    qm_u8_t slave,
    qm_u32_t *sector_count)
{
    qm_u16_t identify[IDE_WORDS_PER_SECTOR];
    qm_u8_t status;
    qm_u32_t timeout;
    qm_u32_t sectors;

    if (sector_count == 0)
        return -1;

    ide_select(slave);

    ide_outb(IDE_SECCOUNT0, 0);
    ide_outb(IDE_LBA0, 0);
    ide_outb(IDE_LBA1, 0);
    ide_outb(IDE_LBA2, 0);
    ide_outb(IDE_COMMAND, IDE_CMD_IDENTIFY);

    status = ide_inb(IDE_STATUS);

    if (status == 0 || status == 0xFF)
        return -1;

    for (timeout = 0; timeout < IDE_TIMEOUT; ++timeout) {
        status = ide_inb(IDE_STATUS);

        if (status == 0 || status == 0xFF)
            return -1;

        if (!(status & IDE_STATUS_BSY))
            break;
    }

    if (timeout == IDE_TIMEOUT)
        return -1;

    /*
     * ATAPI devices return nonzero LBA1/LBA2 signatures.
     * This initial driver intentionally supports ATA disks only.
     */
    if (ide_inb(IDE_LBA1) != 0 || ide_inb(IDE_LBA2) != 0)
        return -1;

    if (ide_wait(1) != 0)
        return -1;

    ide_insw(IDE_DATA, identify, IDE_WORDS_PER_SECTOR);

    if (ide_wait_complete() != 0)
        return -1;


    /* LBA support is word 49 bit 9. */
    if (!(identify[49] & (1U << 9)))
        return -1;

    sectors = (qm_u32_t)identify[60] |
              ((qm_u32_t)identify[61] << 16);

    if (sectors == 0)
        return -1;

    if (sectors > 0x10000000U)
        sectors = 0x10000000U;

    *sector_count = sectors;
    return 0;
}

static int ide_transfer_one(
    qm_u8_t slave,
    qm_u32_t lba,
    void *buffer,
    int write)
{
    qm_u8_t command;

    if (buffer == 0 || lba > IDE_MAX_LBA28)
        return -1;

    ide_select(slave);

    if (ide_wait(0) != 0)
        return -1;

    ide_outb(IDE_FEATURES, 0);
    ide_outb(IDE_SECCOUNT0, 1);
    ide_outb(IDE_LBA0, (qm_u8_t)(lba & 0xFF));
    ide_outb(IDE_LBA1, (qm_u8_t)((lba >> 8) & 0xFF));
    ide_outb(IDE_LBA2, (qm_u8_t)((lba >> 16) & 0xFF));

    ide_outb(
        IDE_HDDEVSEL,
        (qm_u8_t)(
            0xE0 |
            (slave << 4) |
            ((lba >> 24) & 0x0F)
        )
    );

    ide_io_delay();

    command = write ? IDE_CMD_WRITE : IDE_CMD_READ;
    ide_outb(IDE_COMMAND, command);

    if (ide_wait(1) != 0)
        return -1;

    if (write) {
        ide_outsw(
            IDE_DATA,
            buffer,
            IDE_WORDS_PER_SECTOR
        );

        if (ide_wait_complete() != 0)
            return -1;
    } else {
        ide_insw(
            IDE_DATA,
            buffer,
            IDE_WORDS_PER_SECTOR
        );

        if (ide_wait_complete() != 0)
            return -1;
    }

    return 0;
}

static qm_status_t ide_read(
    qm_block_device_id_t id,
    qm_u64_t lba,
    qm_u32_t count,
    void *buffer)
{
    qm_u8_t slave;
    qm_u8_t *destination = (qm_u8_t *)buffer;
    qm_u32_t i;

    if (id == QM_IDE_PRIMARY_MASTER)
        slave = IDE_DEVICE_MASTER;
    else if (id == QM_IDE_PRIMARY_SLAVE)
        slave = IDE_DEVICE_SLAVE;
    else
        return (qm_status_t)-1;

    if (!ide_devices[slave].present)
        return (qm_status_t)-1;

    if (buffer == 0 && count != 0)
        return (qm_status_t)-1;

    if (lba > IDE_MAX_LBA28 ||
        lba > (qm_u64_t)ide_devices[slave].sectors ||
        (qm_u64_t)count >
            (qm_u64_t)ide_devices[slave].sectors - lba ||
        lba > 0x10000000ULL ||
        (qm_u64_t)count > 0x10000000ULL - lba)
        return (qm_status_t)-1;

    for (i = 0; i < count; ++i) {
        if (ide_transfer_one(
                slave,
                (qm_u32_t)(lba + i),
                destination + ((qm_size_t)i * IDE_SECTOR_SIZE),
                0) != 0)
            return (qm_status_t)-2;
    }

    return (qm_status_t)0;
}

static qm_status_t ide_write(
    qm_block_device_id_t id,
    qm_u64_t lba,
    qm_u32_t count,
    const void *buffer)
{
    qm_u8_t slave;
    const qm_u8_t *source = (const qm_u8_t *)buffer;
    qm_u32_t i;

    if (id == QM_IDE_PRIMARY_MASTER)
        slave = IDE_DEVICE_MASTER;
    else if (id == QM_IDE_PRIMARY_SLAVE)
        slave = IDE_DEVICE_SLAVE;
    else
        return (qm_status_t)-1;

    if (!ide_devices[slave].present)
        return (qm_status_t)-1;

    if (buffer == 0 && count != 0)
        return (qm_status_t)-1;

    if (lba > IDE_MAX_LBA28 ||
        lba > (qm_u64_t)ide_devices[slave].sectors ||
        (qm_u64_t)count >
            (qm_u64_t)ide_devices[slave].sectors - lba ||
        lba > 0x10000000ULL ||
        (qm_u64_t)count > 0x10000000ULL - lba)
        return (qm_status_t)-1;

    for (i = 0; i < count; ++i) {
        if (ide_transfer_one(
                slave,
                (qm_u32_t)(lba + i),
                (void *)(source + ((qm_size_t)i * IDE_SECTOR_SIZE)),
                1) != 0)
            return (qm_status_t)-2;
    }

    return (qm_status_t)0;
}

static qm_status_t ide_flush(qm_block_device_id_t id)
{
    qm_u8_t slave;

    if (id == QM_IDE_PRIMARY_MASTER)
        slave = IDE_DEVICE_MASTER;
    else if (id == QM_IDE_PRIMARY_SLAVE)
        slave = IDE_DEVICE_SLAVE;
    else
        return (qm_status_t)-1;

    if (!ide_devices[slave].present)
        return (qm_status_t)-1;

    ide_select(slave);

    if (ide_wait(0) != 0)
        return (qm_status_t)-1;
    ide_outb(IDE_COMMAND, IDE_CMD_FLUSH);

    if (ide_wait_complete() != 0)
        return (qm_status_t)-1;

    return (qm_status_t)0;
}

static const qm_block_device_ops_t ide_block_ops = {
    .read = ide_read,
    .write = ide_write,
    .flush = ide_flush
};


static int ide_name_equal(const char *a, const char *b)
{
    if (a == 0 || b == 0)
        return 0;

    while (*a != '\0' && *b != '\0') {
        if (*a != *b)
            return 0;
        ++a;
        ++b;
    }

    return *a == *b;
}

static qm_status_t ide_register_disk(
    qm_u8_t slave,
    qm_u32_t sectors)
{
    qm_block_device_info_t info;
    qm_block_device_info_t existing;
    qm_block_device_id_t id;
    qm_status_t status;

    if (slave != IDE_DEVICE_MASTER &&
        slave != IDE_DEVICE_SLAVE)
        return (qm_status_t)-1;

    if (sectors == 0 || sectors > 0x10000000U)
        return (qm_status_t)-1;

    id = slave == IDE_DEVICE_MASTER
       ? QM_IDE_PRIMARY_MASTER
       : QM_IDE_PRIMARY_SLAVE;

    info.id = id;
    info.block_count = sectors;
    info.block_size = IDE_SECTOR_SIZE;
    info.flags = 0;
    info.name = slave == IDE_DEVICE_MASTER
              ? "ide-primary-master"
              : "ide-primary-slave";

    status = qm_block_get_info(id, &existing);

    if (status == 0) {
        if (existing.id == info.id &&
            existing.block_count == info.block_count &&
            existing.block_size == info.block_size &&
            existing.flags == info.flags &&
            ide_name_equal(existing.name, info.name))
            return (qm_status_t)0;

        status = qm_block_device_unregister(id);
        if (status != 0)
            return status;
    } else if (status != (qm_status_t)-1) {
        return status;
    }

    return qm_block_device_register(&info, &ide_block_ops);
}

qm_status_t qm_ide_pio_init(void)
{
qm_u32_t master_sectors = 0;
    qm_u32_t slave_sectors = 0;
    int master_found;
    int slave_found;
    int registered = 0;

    /* This driver uses polling, not IDE channel interrupts. */
    ide_outb(IDE_CONTROL, 0x02);

    /*
     * Probe both devices before changing their published state.
     * A failed probe does not erase a previously working registration.
     */
    master_found = ide_identify(
        IDE_DEVICE_MASTER,
        &master_sectors
    );

    slave_found = ide_identify(
        IDE_DEVICE_SLAVE,
        &slave_sectors
    );

    if (master_found == 0 &&
        ide_register_disk(IDE_DEVICE_MASTER, master_sectors) == 0) {
        ide_devices[IDE_DEVICE_MASTER].sectors = master_sectors;
        ide_devices[IDE_DEVICE_MASTER].present = 1;
        ++registered;
    }

    if (slave_found == 0 &&
        ide_register_disk(IDE_DEVICE_SLAVE, slave_sectors) == 0) {
        ide_devices[IDE_DEVICE_SLAVE].sectors = slave_sectors;
        ide_devices[IDE_DEVICE_SLAVE].present = 1;
        ++registered;
    }

    /*
     * Do not clear old state merely because a later probe timed out.
     * A transient probe failure must not silently disable a registered disk.
     */
    if (registered != 0 ||
        ide_devices[IDE_DEVICE_MASTER].present ||
        ide_devices[IDE_DEVICE_SLAVE].present)
        return (qm_status_t)0;

    return (qm_status_t)-1;
}
