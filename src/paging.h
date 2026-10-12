#ifndef QMJYOS_PAGING_H
#define QMJYOS_PAGING_H

#include <stddef.h>
#include <stdint.h>

#define VM_PAGE_SIZE       4096ULL
#define VM_PRESENT         (1ULL << 0)
#define VM_WRITE           (1ULL << 1)
#define VM_USER            (1ULL << 2)
#define VM_PWT             (1ULL << 3)
#define VM_PCD             (1ULL << 4)
#define VM_ACCESSED        (1ULL << 5)
#define VM_DIRTY           (1ULL << 6)
#define VM_GLOBAL          (1ULL << 8)
#define VM_NO_EXECUTE      (1ULL << 63)
#define VM_ADDR_MASK       0x000FFFFFFFFFF000ULL
#define VM_LEAF_FLAG_MASK  (VM_WRITE | VM_USER | VM_PWT | VM_PCD | VM_GLOBAL | VM_NO_EXECUTE)

enum vm_status {
    VM_OK = 0,
    VM_ERR_ARGUMENT = -1,
    VM_ERR_ALIGNMENT = -2,
    VM_ERR_EXISTS = -3,
    VM_ERR_NOT_MAPPED = -4,
    VM_ERR_NO_MEMORY = -5,
    VM_ERR_FLAGS = -6,
    VM_ERR_ADDRESS = -7,
    VM_ERR_CORRUPT = -8
};

/*
 * Early inactive page-table builder for the current identity-mapped kernel.
 * vm_init() creates a fresh, separate root and does not load CR3.
 * Page-table pool storage is static and must remain identity-accessible.
 * This module does not allocate/free data frames or activate a root.
 */
int vm_init(void);
int vm_map_page(uint64_t virtual_address, uint64_t physical_address, uint64_t flags);
int vm_unmap_page(uint64_t virtual_address);
int vm_query_page(uint64_t virtual_address, uint64_t *physical_address, uint64_t *flags);
uint64_t vm_root_physical(void);
size_t vm_tables_used(void);
size_t vm_tables_capacity(void);
int vm_self_test(void);

#endif
