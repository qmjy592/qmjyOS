#ifndef QMJY_IDE_PIO_H
#define QMJY_IDE_PIO_H

#include "../../include/qmjy/block.h"

/*
 * Legacy ATA IDE PIO driver.
 *
 * Device IDs:
 *   0x100 - primary channel master
 *   0x101 - primary channel slave
 *
 * Sector size: 512 bytes.
 * Addressing: LBA28.
 */

#define QM_IDE_PRIMARY_MASTER 0x100ULL
#define QM_IDE_PRIMARY_SLAVE  0x101ULL

qm_status_t qm_ide_pio_init(void);

#endif
