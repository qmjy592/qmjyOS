#ifndef QMJY_PROCESS_RUNTIME_H
#define QMJY_PROCESS_RUNTIME_H

#include "qmjy/process.h"
#include "paging.h"

#define QM_PROCESS_RUNTIME_MAX 8U
#define QM_PROCESS_RUNTIME_MAX_PAGES 128U
#define QM_PROCESS_RUNTIME_STACK_TOP VM_USER_MAX_VA

/*
 * Bounded first-generation process runtime.
 * Each process owns a private PML4, up to 128 user frames (512 KiB),
 * one user stack page, and the frames backing its validated ELF segments.
 */
typedef struct qm_process_runtime_info {
    qm_process_handle_t id;
    qm_u64_t root_physical;
    qm_u64_t entry;
    qm_u64_t user_stack_pointer;
    qm_u32_t owned_page_count;
    qm_process_state_t state;
} qm_process_runtime_info_t;

qm_status_t qm_process_runtime_init(void);
qm_status_t qm_process_runtime_load(const char *name,
                                    const void *elf_bytes,
                                    qm_size_t elf_size,
                                    qm_process_handle_t *out_id);
qm_status_t qm_process_runtime_query(qm_process_handle_t id,
                                     qm_process_runtime_info_t *out_info);
qm_status_t qm_process_runtime_query_page(qm_process_handle_t id,
                                          qm_u64_t virtual_address,
                                          qm_u64_t *physical_address,
                                          qm_u64_t *flags);
qm_status_t qm_process_runtime_user_range_check(qm_process_handle_t id,
                                                qm_u64_t user_address,
                                                qm_size_t length,
                                                int write_access);
qm_status_t qm_process_runtime_activate(qm_process_handle_t id);
qm_status_t qm_process_runtime_exit_current(qm_s32_t exit_code);
qm_status_t qm_process_runtime_terminate(qm_process_handle_t id,
                                         qm_s32_t exit_code);
qm_status_t qm_process_runtime_reap(qm_process_handle_t id);

#endif
