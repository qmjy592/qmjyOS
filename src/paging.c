#include "paging.h"

#define VM_TABLE_COUNT 64U
#define VM_ENTRIES 512U
#define VM_LARGE_PAGE (1ULL << 7)
#define VM_LOW_CANONICAL_LIMIT (1ULL << 47)

/* Each page-table page is aligned and lies in the kernel's first-GiB identity map. */
static uint64_t table_pool[VM_TABLE_COUNT][VM_ENTRIES]
    __attribute__((aligned(VM_PAGE_SIZE)));
static size_t tables_used;
static unsigned char table_in_use[VM_TABLE_COUNT];
static uint64_t *root_table;

struct walk_path {
    uint64_t *tables[4];
    unsigned int indices[4];
    size_t created_count;
    uint64_t *created[3];
    size_t promoted_count;
    uint64_t *promoted_entries[3];
    uint64_t promoted_original[3];
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
    for (size_t i = 0; i < VM_TABLE_COUNT; ++i) {
        if (!table_in_use[i]) {
            table_in_use[i] = 1;
            ++tables_used;
            zero_table(table_pool[i]);
            return table_pool[i];
        }
    }
    return 0;
}

static void release_table(uint64_t *table) {
    for (size_t i = 0; i < VM_TABLE_COUNT; ++i) {
        if (table == table_pool[i] && table_in_use[i]) {
            zero_table(table);
            table_in_use[i] = 0;
            --tables_used;
            return;
        }
    }
}

static int table_is_empty(const uint64_t *table) {
    for (size_t i = 0; i < VM_ENTRIES; ++i)
        if (table[i] & VM_PRESENT) return 0;
    return 1;
}

static uint64_t *entry_table(uint64_t entry) {
    uint64_t address = entry & VM_ADDR_MASK;
    /* The early pool and all created tables must be kernel-owned pool pages. */
    for (size_t i = 0; i < VM_TABLE_COUNT; ++i) {
        if (table_in_use[i] &&
            (uint64_t)(uintptr_t)table_pool[i] == address) return table_pool[i];
    }
    return 0;
}

static void rollback_created(struct walk_path *path) {
    while (path->promoted_count != 0) {
        size_t n = --path->promoted_count;
        *path->promoted_entries[n] = path->promoted_original[n];
    }

    while (path->created_count != 0) {
        size_t n = --path->created_count;
        uint64_t *table = path->created[n];
        uint64_t *parent = path->tables[n];
        unsigned int index = path->indices[n];
        parent[index] = 0;
        release_table(table);
    }
}

static int walk(uint64_t va, int create, uint64_t flags,
                struct walk_path *path, uint64_t **leaf_entry) {
    static const unsigned int shifts[3] = {39, 30, 21};
    uint64_t *table = root_table;
    path->created_count = 0;
    path->promoted_count = 0;
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

            /*
             * A user leaf requires USER on every ancestor.
             * Promote only the ancestor permission bit; preserve the
             * physical address and every other existing entry flag.
             * Supervisor-only leaf PTEs remain supervisor-only.
             */
            if ((flags & VM_USER) && !(entry & VM_USER)) {
                if (path->promoted_count >= 3) {
                    return VM_ERR_CORRUPT;
                }
                size_t n = path->promoted_count++;
                path->promoted_entries[n] = &table[index];
                path->promoted_original[n] = entry;
                entry |= VM_USER;
                table[index] = entry;
            }
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
    for (size_t i = 0; i < VM_TABLE_COUNT; ++i) {
        zero_table(table_pool[i]);
        table_in_use[i] = 0;
    }
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
    /* Reclaim empty PT/PD/PDPT pages from the bottom upward; keep the root. */
    for (int level = 3; level >= 1; --level) {
        uint64_t *child = path.tables[level];
        uint64_t *parent = path.tables[level - 1];
        unsigned int parent_index = path.indices[level - 1];
        if (!table_is_empty(child)) break;
        parent[parent_index] = 0;
        release_table(child);
    }
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
    const uint64_t user_va = 0x0000000080000000ULL;
    const uint64_t user_pa = 0x0000000020000000ULL;
    uint64_t queried_pa = 0;
    uint64_t queried_flags = 0;

    /* Test 1: clean initialization and a normal 4 KiB mapping. */
    if (vm_init() != VM_OK) return -1;
    if (vm_tables_used() != 1) return -2;

    if (vm_map_page(test_va, test_pa, test_flags) != VM_OK) return -3;
    if (vm_query_page(test_va + 0x123, &queried_pa, &queried_flags) != VM_OK)
        return -4;
    if (queried_pa != test_pa + 0x123) return -5;
    if ((queried_flags & (VM_PRESENT | VM_WRITE | VM_NO_EXECUTE)) !=
        (VM_PRESENT | VM_WRITE | VM_NO_EXECUTE)) return -6;

    /* Test 2: duplicate mappings and invalid arguments must be rejected. */
    if (vm_map_page(test_va, test_pa, test_flags) != VM_ERR_EXISTS) return -7;
    if (vm_map_page(test_va + 1, test_pa, test_flags) != VM_ERR_ALIGNMENT)
        return -8;
    if (vm_map_page(test_va, test_pa + 1, test_flags) != VM_ERR_ALIGNMENT)
        return -9;
    if (vm_map_page(VM_LOW_CANONICAL_LIMIT, test_pa, test_flags) != VM_ERR_ADDRESS)
        return -10;
    if (vm_map_page(test_va + VM_PAGE_SIZE, test_pa,
                    VM_PRESENT) != VM_ERR_FLAGS) return -11;

    /* Test 3: user permission must be present in every ancestor entry. */
    if (vm_map_page(user_va, user_pa, VM_USER | VM_WRITE | VM_NO_EXECUTE)
        != VM_OK) return -12;
    if (vm_query_page(user_va, &queried_pa, &queried_flags) != VM_OK)
        return -13;
    if (queried_pa != user_pa) return -14;
    if ((queried_flags & (VM_USER | VM_WRITE | VM_NO_EXECUTE)) !=
        (VM_USER | VM_WRITE | VM_NO_EXECUTE)) return -15;

    /*
     * Adding a user mapping may promote ancestor USER bits. The existing
     * supervisor leaf must nevertheless remain supervisor-only.
     */
    if (vm_query_page(test_va, &queried_pa, &queried_flags) != VM_OK)
        return -16;
    if (queried_pa != test_pa) return -17;
    if (queried_flags & VM_USER) return -18;

    /* A user mapping may share the PML4/PDPT path with supervisor mappings. */
    if (vm_map_page(0x0000000040100000ULL, 0x0000000030100000ULL,
                    VM_USER | VM_WRITE) != VM_OK) return -19;
    if (vm_query_page(0x0000000040100000ULL,
                      &queried_pa, &queried_flags) != VM_OK) return -20;
    if (!(queried_flags & VM_USER)) return -21;
    if (vm_query_page(test_va, &queried_pa, &queried_flags) != VM_OK)
        return -22;
    if (queried_flags & VM_USER) return -23;

    /* Test 4: unmapping removes the leaf and query reports it absent. */
    if (vm_unmap_page(test_va) != VM_OK) return -17;
    if (vm_query_page(test_va, &queried_pa, &queried_flags) != VM_ERR_NOT_MAPPED)
        return -18;

    /* Test 5: a non-page-aligned query still returns the physical offset. */
    if (vm_query_page(user_va + 0x321, &queried_pa, &queried_flags) != VM_OK)
        return -19;
    if (queried_pa != user_pa + 0x321) return -20;

    /* Test 6: force real pool exhaustion, then verify failed USER promotion rolls back. */
    if (vm_init() != VM_OK) return -24;
    {
        const uint64_t base_va = 0x0000000040000000ULL; /* 1 GiB */
        const uint64_t base_pa = 0x0000000010000000ULL;
        const size_t mappings = VM_TABLE_COUNT - 3U;
        size_t mapped = 0;
        uint64_t target_va;
        uint64_t *pdpt;
        int status;

        /* First mapping consumes root + PDPT + PD + PT; each later 2 MiB
         * stride needs one additional PT under the same PD. */
        for (size_t i = 0; i < mappings; ++i) {
            uint64_t va = base_va + (uint64_t)i * 0x200000ULL;
            uint64_t pa = base_pa + (uint64_t)i * VM_PAGE_SIZE;
            status = vm_map_page(va, pa, VM_WRITE | VM_NO_EXECUTE);
            if (status != VM_OK) return -25;
            ++mapped;
        }
        if (vm_tables_used() != vm_tables_capacity()) return -26;

        /* This address shares the existing PML4/PDPT path but needs a new PT.
         * USER bits are tentatively promoted before allocation fails. */
        target_va = base_va + (uint64_t)mappings * 0x200000ULL;
        status = vm_map_page(target_va, base_pa + 0x1000000ULL,
                             VM_USER | VM_WRITE);
        if (status != VM_ERR_NO_MEMORY) return -27;
        if (vm_tables_used() != vm_tables_capacity()) return -28;
        if (vm_query_page(target_va, &queried_pa, &queried_flags) !=
            VM_ERR_NOT_MAPPED) return -29;

        /* Assert the ancestor permission bits were restored exactly. */
        if (root_table[0] & VM_USER) return -30;
        pdpt = entry_table(root_table[0]);
        if (!pdpt || (pdpt[1] & VM_USER)) return -31;

        /* All mappings must remain queryable, then full unmap must reclaim
         * every intermediate table and leave only the root. */
        if (vm_query_page(base_va, &queried_pa, &queried_flags) != VM_OK ||
            queried_pa != base_pa) return -32;
        for (size_t i = 0; i < mapped; ++i) {
            uint64_t va = base_va + (uint64_t)i * 0x200000ULL;
            if (vm_unmap_page(va) != VM_OK) return -33;
        }
        if (vm_tables_used() != 1) return -34;
    }

    return VM_OK;
}
