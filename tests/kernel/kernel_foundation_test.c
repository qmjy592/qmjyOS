#include "qmjy/kernel.h"
#include "qmjy/syscall.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static unsigned int g_order[4];
static size_t g_order_count;

static qm_status_t early_one(void) { g_order[g_order_count++] = 1; return QM_OK; }
static qm_status_t early_two(void) { g_order[g_order_count++] = 2; return QM_OK; }
static qm_status_t core_init(void) { g_order[g_order_count++] = 3; return QM_OK; }

static qm_boot_info_t valid_boot_info(void)
{
    qm_boot_info_t info = {0};
    info.magic = QM_BOOT_INFO_MAGIC;
    info.protocol_major = QM_BOOT_PROTOCOL_MAJOR;
    info.protocol_minor = QM_BOOT_PROTOCOL_MINOR;
    info.struct_size = (qm_u32_t)offsetof(qm_boot_info_t, reserved);
    info.architecture = QM_BOOT_ARCH_X86_64;
    info.firmware = QM_BOOT_FIRMWARE_BIOS;
    info.memory_map_physical = 0x10000;
    info.memory_map_count = 1;
    info.memory_map_entry_size = sizeof(qm_boot_memory_region_t);
    info.kernel_physical_base = 0x200000;
    info.kernel_physical_size = 0x100000;
    info.kernel_virtual_base = 0x200000;
    info.kernel_entry_virtual = 0x200170;
    return info;
}

int main(void)
{
    qm_boot_info_t info = valid_boot_info();
    qm_kernel_init_entry_t e1 = {"early-one", QM_KERNEL_INIT_EARLY, early_one};
    qm_kernel_init_entry_t e2 = {"early-two", QM_KERNEL_INIT_EARLY, early_two};
    qm_kernel_init_entry_t e3 = {"core", QM_KERNEL_INIT_CORE, core_init};
    qm_kernel_init_entry_t invalid = {"bad-level", (qm_kernel_init_level_t)99, core_init};

    assert(qm_boot_validate_info(&info) == QM_OK);
    info.protocol_major++;
    assert(qm_boot_validate_info(&info) == QM_ERR_NOT_SUPPORTED);
    info = valid_boot_info();
    info.memory_map_physical = UINT64_MAX - 8;
    assert(qm_boot_validate_info(&info) == QM_ERR_OVERFLOW);
    info = valid_boot_info();
    info.memory_map_entry_size = 1;
    assert(qm_boot_validate_info(&info) == QM_ERR_CORRUPT);
    info = valid_boot_info();
    info.kernel_virtual_base = UINT64_MAX - 0x7FFU;
    info.kernel_physical_size = 0x1000U;
    assert(qm_boot_validate_info(&info) == QM_ERR_OVERFLOW);
    info = valid_boot_info();
    info.kernel_entry_virtual = info.kernel_virtual_base - 1U;
    assert(qm_boot_validate_info(&info) == QM_ERR_CORRUPT);
    info = valid_boot_info();
    info.kernel_entry_virtual = info.kernel_virtual_base +
                                info.kernel_physical_size;
    assert(qm_boot_validate_info(&info) == QM_ERR_CORRUPT);
    info = valid_boot_info();

    assert(qm_kernel_init_register(NULL) == QM_ERR_INVALID);
    assert(qm_kernel_init_register(&e1) == QM_OK);
    assert(qm_kernel_init_register(&e1) == QM_ERR_EXISTS);
    assert(qm_kernel_init_register(&e2) == QM_OK);
    assert(qm_kernel_init_register(&e3) == QM_OK);
    assert(qm_kernel_init_register(&invalid) == QM_ERR_INVALID);
    assert(qm_kernel_init_run_level(QM_KERNEL_INIT_CORE) == QM_ERR_DEPENDENCY);

    assert(qm_kernel_init(&info) == QM_OK);
    assert(g_order_count == 3);
    assert(g_order[0] == 1 && g_order[1] == 2 && g_order[2] == 3);
    assert(qm_kernel_init_run_level(QM_KERNEL_INIT_EARLY) == QM_OK);
    assert(g_order_count == 3); /* A completed level is not executed twice. */
    assert(qm_kernel_init(&info) == QM_ERR_STATE);

    puts("Kernel boot-info and init-registry tests: PASS");
    return 0;
}
