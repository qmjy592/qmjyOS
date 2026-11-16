#include "qmjy/kernel.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

static unsigned int g_called;
static qm_status_t fail_init(void) { ++g_called; return QM_ERR_IO; }
static qm_status_t must_not_run(void) { ++g_called; return QM_OK; }

int main(void)
{
    qm_kernel_init_entry_t first = {"failing", QM_KERNEL_INIT_EARLY, fail_init};
    qm_kernel_init_entry_t second = {"later", QM_KERNEL_INIT_EARLY, must_not_run};
    assert(qm_kernel_init_register(&first) == QM_OK);
    assert(qm_kernel_init_register(&second) == QM_OK);
    assert(qm_kernel_init_run_level(QM_KERNEL_INIT_EARLY) == QM_ERR_IO);
    assert(g_called == 1);
    assert(qm_kernel_init_run_level(QM_KERNEL_INIT_EARLY) == QM_ERR_IO);
    assert(g_called == 1);
    puts("Kernel init failure propagation test: PASS");
    return 0;
}
