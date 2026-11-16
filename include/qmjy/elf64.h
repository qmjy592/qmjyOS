#ifndef QMJY_ELF64_H
#define QMJY_ELF64_H

#include "qmjy/types.h"
#include "qmjy/error.h"

#define QM_ELF64_MAX_SEGMENTS 16U
#define QM_ELF64_PAGE_SIZE 4096ULL
#define QM_ELF64_USER_MIN 0x0000000000010000ULL
#define QM_ELF64_USER_MAX 0x00007FFFFFFF0000ULL

#define QM_ELF64_PROT_READ  0x01U
#define QM_ELF64_PROT_WRITE 0x02U
#define QM_ELF64_PROT_EXEC  0x04U

typedef struct qm_elf64_segment {
    qm_u64_t virtual_address;
    qm_u64_t map_address;
    qm_u64_t map_size;
    qm_u64_t memory_size;
    qm_u64_t file_size;
    qm_u64_t file_offset;
    qm_u32_t protection;
    qm_u32_t reserved;
} qm_elf64_segment_t;

typedef struct qm_elf64_image {
    qm_u64_t entry;
    qm_u32_t segment_count;
    qm_u32_t reserved;
    qm_elf64_segment_t segments[QM_ELF64_MAX_SEGMENTS];
} qm_elf64_image_t;

typedef qm_status_t (*qm_elf64_map_fn)(
    void *context,
    qm_u64_t virtual_base,
    qm_u64_t size,
    qm_u32_t protection,
    void **writable_view
);

typedef qm_status_t (*qm_elf64_unmap_fn)(
    void *context,
    qm_u64_t virtual_base,
    qm_u64_t size
);

typedef struct qm_elf64_memory_ops {
    qm_elf64_map_fn map;
    qm_elf64_unmap_fn unmap;
} qm_elf64_memory_ops_t;

/*
 * Loads a strict, little-endian x86_64 ET_EXEC image through caller-provided
 * page mapping callbacks. PT_LOAD segments are page-rounded, validated for
 * overlap, user-range bounds and W^X, then copied and BSS-zeroed.
 * Dynamic linking, relocations, PT_INTERP and TLS are deliberately rejected.
 * A map callback that returns an error must leave that range unmapped; if it
 * returns QM_OK it must provide a writable view covering the complete range.
 */
qm_status_t qm_elf64_load(
    const void *file_bytes,
    qm_size_t file_size,
    const qm_elf64_memory_ops_t *memory_ops,
    void *memory_context,
    qm_elf64_image_t *out_image
);

qm_status_t qm_elf64_unload(
    const qm_elf64_memory_ops_t *memory_ops,
    void *memory_context,
    const qm_elf64_image_t *image
);

#endif
