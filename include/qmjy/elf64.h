#ifndef QMJY_ELF64_H
#define QMJY_ELF64_H

#include "qmjy/types.h"
#include "qmjy/error.h"

#define QM_ELF64_MAX_SEGMENTS 16U
#define QM_ELF64_PAGE_SIZE 4096ULL
#define QM_ELF64_USER_MIN 0x0000000040000000ULL
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
 * QMJYOS_ELF64_HARDENING_CONTRACT_V1
 * Loads strict little-endian x86_64 ET_EXEC images only. PT_DYNAMIC,
 * relocations, PT_INTERP, PT_SHLIB, PT_TLS and PT_GNU_RELRO are rejected.
 * PT_GNU_STACK is accepted only when it is not executable; the actual user
 * stack is created and protected by the process/address-space subsystem.
 *
 * The loader validates all headers, file ranges, page-rounded user mappings,
 * segment overlap, W^X permissions and the executable entry point before
 * invoking map(). The output image is zeroed on every load failure.
 *
 * map() must be all-or-nothing: an error means the requested range remains
 * unmapped; QM_OK means the complete range is mapped and writable_view covers
 * the complete range. QM_OK with a NULL view is treated as a broken mapping
 * result and the loader attempts to unmap that range.
 *
 * unmap() must be all-or-nothing: QM_OK removes the complete range; an error
 * leaves it mapped. Rollback/unload failures can leave address-space resources
 * behind, so the caller must destroy the affected address space on QM_ERR_IO.
 * Callers must discard an image descriptor after any unload attempt because
 * unload is not transactional and may have removed only some of its ranges.
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
