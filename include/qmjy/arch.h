#ifndef QMJY_ARCH_H
#define QMJY_ARCH_H

#include "types.h"
#include "error.h"

typedef qm_u64_t qm_irq_state_t;
typedef qm_u64_t qm_cpu_id_t;

typedef struct qm_arch_ops {
    qm_status_t (*early_init)(void);
    qm_status_t (*late_init)(void);
    qm_cpu_id_t (*cpu_id)(void);
    qm_irq_state_t (*irq_save)(void);
    void (*irq_restore)(qm_irq_state_t state);
    void (*halt)(void);
    void (*relax)(void);
    qm_u64_t (*monotonic_time_ns)(void);
    void (*tlb_flush_all)(void);
    void (*address_space_activate)(qm_phys_addr_t root);
} qm_arch_ops_t;

qm_status_t qm_arch_register(const qm_arch_ops_t *ops);
const qm_arch_ops_t *qm_arch_get_ops(void);

qm_status_t qm_arch_early_init(void);
qm_status_t qm_arch_late_init(void);

qm_cpu_id_t qm_arch_cpu_id(void);
qm_irq_state_t qm_arch_irq_save(void);
void qm_arch_irq_restore(qm_irq_state_t state);

void qm_arch_halt(void);
void qm_arch_relax(void);

#endif
