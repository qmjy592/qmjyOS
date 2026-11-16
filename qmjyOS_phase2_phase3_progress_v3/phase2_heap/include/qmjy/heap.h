#ifndef QMJY_HEAP_H
#define QMJY_HEAP_H

#include "qmjy/types.h"
#include "qmjy/error.h"

/*
 * Initial bounded kernel heap.
 *
 * The first implementation uses a statically reserved arena so it can be
 * brought up before the PMM-backed virtual allocator is complete. All
 * returned pointers are aligned to QM_HEAP_ALIGNMENT (16 bytes on x86_64).
 * The allocator is not internally synchronized: callers must serialize
 * access until the kernel lock/preemption layer is available.
 */
#ifndef QM_HEAP_SIZE
#define QM_HEAP_SIZE (256U * 1024U)
#endif

#define QM_HEAP_ALIGNMENT 16U

typedef struct qm_heap_stats {
    qm_size_t arena_bytes;
    qm_size_t free_bytes;
    qm_size_t largest_free_block;
    qm_size_t allocated_blocks;
    qm_size_t free_blocks;
} qm_heap_stats_t;

qm_status_t qm_heap_init(void);
void *kmalloc(qm_size_t size);
void *kcalloc(qm_size_t count, qm_size_t size);
qm_status_t kfree(void *pointer);
qm_status_t qm_heap_get_stats(qm_heap_stats_t *stats);

#endif
