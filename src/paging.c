#include "paging.h"

#define VM_TABLE_COUNT 64U
#define VM_ENTRIES 512U
#define VM_LARGE_PAGE (1ULL << 7)
#define VM_LOW_CANONICAL_LIMIT (1ULL << 47)

/* Each page-table page is aligned and lies in the kernel's first-GiB identity map. */
static uint64_t table_pool[VM_TABLE_COUNT][VM_ENTRIES]
    __attribute__((aligned(VM_PAGE_SIZE)));
static size_t tables_used;
static uint64_t *root_table;

struct walk_path {
    uint64_t *tables[4];
    unsigned int indices[4];
    size_t created_count;
    uint64_t *created[3];
};

static int is_page_aligned(uint64_t value) {
    return (value & (VM_PAGE_SIZE - 1)) == 0;
}

static int is_low_canonical(uint64_t va) {
    /* Deliberately support only the lower canonical half, [0, 2^47). */
    return va < VM_LOW_CANONICAL_LIMIT;
}

static int is_valid_physical_page(uint64_t pa) {
    return is_page_aligned(pa) && (pa & ~VM_ADDR_MASK) == 0;
}

static unsigned int page_index(uint64_t va, unsigned int shift) {
    return (unsigned int)((va >> shift) & 0x1FFU);
}

static void zero_table(uint64_t *table) {
    for (size_t i = 0; i < VM_ENTRIES; ++i) table[i] = 0;
}

static uint64_t *allocate_table(void) {
    uint64_t *table;
    if (tables_used >= VM_TABLE_COUNT) return 0;
    table = table_pool[tables_used++];
    zero_table(table);
    return table;
}

static uint64_t *entry_table(uint64_t entry) {
    uint64_t address = entry & VM_ADDR_MASK;
    /* The early pool and all created tables must be kernel-owned pool pages. */
    for (size_t i = 0; i < tables_used; ++i) {
        if ((uint64_t)(uintptr_t)table_pool[i] == address) return table_pool[i];
    }
    return 0;
}

static void rollback_created(struct walk_path *path) {
    while (path->created_count != 0) {
        size_t n = --path->created_count;
        uint64_t *table = path->created[n];
        uint64_t *parent = path->tables[n];
        unsigned int index = path->indices[n];
        parent[index] = 0;
        /* Reclaim only the most recently allocated page; allocations are stack-like. */
        if (tables_used != 0 && table == table_pool[tables_used - 1]) {
            zero_table(table);
            --tables_used;
        }
    }
}

static int walk(uint64_t va, int create, uint64_t flags,
                struct walk_path *path, uint64_t **leaf_entry) {
    static const unsigned int shifts[3] = {39, 30, 21};
    uint64_t *table = root_table;
    path->created_count = 0;
    path->tables[0] = root_table;

    for (size_t level = 0; level < 3; ++level) {
        unsigned int index = page_index(va, shifts[level]);
        uint64_t entry = table[index];
        path->indices[level] = index;
        if (!(entry & VM_PRESENT)) {
            uint64_t *next;
            if (!create) return VM_ERR_NOT_MAPPED;
            next = allocate_table();
            if (!next) return VM_ERR_NO_MEMORY;
            path->created[path->created_count++] = next;
            /* User permission must be present on every level for a user leaf. */
            table[index] = ((uint64_t)(uintptr_t)next & VM_ADDR_MASK) |
                           VM_PRESENT | VM_WRITE | (flags & VM_USER);
            entry = table[index];
        } else {
            if (entry & VM_LARGE_PAGE) return VM_ERR_CORRUPT;
            if ((flags & VM_USER) && !(entry & VM_USER)) return VM_ERR_FLAGS;
        }

        table = entry_table(entry);
        if (!table) return VM_ERR_CORRUPT;
        path->tables[level + 1] = table;
    }

    path->indices[3] = page_index(va, 12);
    *leaf_entry = &table[path->indices[3]];
    return VM_OK;
}

int vm_init(void) {
    /* Reinitialize only this inactive manager's own pool; never touch active CR3. */
    for (size_t i = 0; i < VM_TABLE_COUNT; ++i) zero_table(table_pool[i]);
    tables_used = 0;
    root_table = allocate_table();
    return root_table ? VM_OK : VM_ERR_NO_MEMORY;
}

int vm_map_page(uint64_t va, uint64_t pa, uint64_t flags) {
    struct walk_path path;
    uint64_t *pte = 0;
    int status;
    if (!root_table) return VM_ERR_ARGUMENT;
    if (!is_low_canonical(va)) return VM_ERR_ADDRESS;
    if (!is_page_aligned(va) || !is_page_aligned(pa)) return VM_ERR_ALIGNMENT;
    if (!is_valid_physical_page(pa)) return VM_ERR_ADDRESS;
    if (flags & ~VM_LEAF_FLAG_MASK) return VM_ERR_FLAGS;

    status = walk(va, 1, flags, &path, &pte);
    if (status != VM_OK) {
        rollback_created(&path);
        return status;
    }
    if (*pte & VM_PRESENT) {
        rollback_created(&path);
        return VM_ERR_EXISTS;
    }
    *pte = pa | VM_PRESENT | flags;
    return VM_OK;
}

int vm_unmap_page(uint64_t va) {
    struct walk_path path;
    uint64_t *pte = 0;
    int status;
    if (!root_table) return VM_ERR_ARGUMENT;
    if (!is_low_canonical(va)) return VM_ERR_ADDRESS;
    if (!is_page_aligned(va)) return VM_ERR_ALIGNMENT;
    status = walk(va, 0, 0, &path, &pte);
    if (status != VM_OK) return status;
    if (!(*pte & VM_PRESENT)) return VM_ERR_NOT_MAPPED;
    *pte = 0;
    /* Root is inactive by contract, so no TLB invalidation is required here. */
    return VM_OK;
}

int vm_query_page(uint64_t va, uint64_t *pa, uint64_t *flags) {
    struct walk_path path;
    uint64_t *pte = 0;
    int status;
    if (!root_table || !pa || !flags) return VM_ERR_ARGUMENT;
    if (!is_low_canonical(va)) return VM_ERR_ADDRESS;
    status = walk(va, 0, 0, &path, &pte);
    if (status != VM_OK) return status;
    if (!(*pte & VM_PRESENT)) return VM_ERR_NOT_MAPPED;
    *pa = (*pte & VM_ADDR_MASK) | (va & (VM_PAGE_SIZE - 1));
    *flags = *pte & ~VM_ADDR_MASK;
    return VM_OK;
}

uint64_t vm_root_physical(void) {
    return root_table ? (uint64_t)(uintptr_t)root_table : 0;
}

size_t vm_tables_used(void) { return tables_used; }
size_t vm_tables_capacity(void) { return VM_TABLE_COUNT; }


/* Deterministic smoke test; it edits only the inactive page-table pool. */
int vm_self_test(void) {
    const uint64_t test_va = 0x0000000040000000ULL;
    const uint64_t test_pa = 0x0000000030000000ULL;
    const uint64_t test_flags = VM_WRITE | VM_NO_EXECUTE;
    uint64_t queried_pa = 0;
    uint64_t queried_flags = 0;

    if (vm_init() != VM_OK) return -1;
    if (vm_map_page(test_va, test_pa, test_flags) != VM_OK) return -2;
    if (vm_query_page(test_va + 0x123, &queried_pa, &queried_flags) != VM_OK)
        return -3;
    if (queried_pa != test_pa + 0x123) return -4;
    if ((queried_flags & (VM_PRESENT | VM_WRITE | VM_NO_EXECUTE)) !=
        (VM_PRESENT | VM_WRITE | VM_NO_EXECUTE)) return -5;
    if (vm_map_page(test_va, test_pa, test_flags) != VM_ERR_EXISTS) return -6;
    if (vm_map_page(test_va + 1, test_pa, test_flags) != VM_ERR_ALIGNMENT)
        return -7;
    if (vm_map_page(VM_LOW_CANONICAL_LIMIT, test_pa, test_flags) != VM_ERR_ADDRESS)
        return -8;
    if (vm_unmap_page(test_va) != VM_OK) return -9;
    if (vm_query_page(test_va, &queried_pa, &queried_flags) != VM_ERR_NOT_MAPPED)
        return -10;
    return VM_OK;
}
