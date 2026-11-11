#include "qmjy/types.h"
#include "qmjy/error.h"
#include "qmjy/compiler.h"
#include "qmjy/limits.h"
#include "qmjy/config.h"
#include "qmjy/version.h"

#include "qmjy/arch.h"
#include "qmjy/boot.h"
#include "qmjy/kernel.h"
#include "qmjy/log.h"
#include "qmjy/panic.h"
#include "qmjy/memory.h"
#include "qmjy/task.h"
#include "qmjy/ipc.h"
#include "qmjy/device.h"
#include "qmjy/block.h"
#include "qmjy/fs.h"
#include "qmjy/syscall.h"
#include "qmjy/module.h"
#include "qmjy/test.h"

int main(void)
{
    qm_status_t status = QM_OK;
    qm_boot_info_t boot_info = {0};
    qm_memory_stats_t memory_stats = {0};
    qm_task_info_t task_info = {0};
    qm_test_summary_t test_summary = {0};

    (void)boot_info;
    (void)memory_stats;
    (void)task_info;
    (void)test_summary;

    return status == QM_OK ? 0 : 1;
}
