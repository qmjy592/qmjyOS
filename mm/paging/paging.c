#include "paging.h"

static uint64_t *active_root;

#define VM_TABLE_COUNT 256U
#define VM_ASPACE_MAGIC 0x41535043U
#define VM_ENTRIES 512U
#define VM_LARGE_PAGE (1ULL << 7)
#define VM_LARGE_SIZE (1ULL << 21)
#define VM_LARGE_ADDR_MASK 0x000FFFFFFFE00000ULL
#define VM_LOW_CANONICAL_LIMIT (1ULL << 47)
#define VM_LEAF_FLAGS (VM_PRESENT | VM_WRITE | VM_USER | VM_PWT | VM_PCD | \
                       VM_ACCESSED | VM_DIRTY | VM_GLOBAL | VM_NO_EXECUTE)

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
    uint64_t *created_parent[3];
    unsigned int created_index[3];
    uint64_t created_original[3];
    size_t promoted_count;
    uint64_t *promoted_entries[3];
    uint64_t promoted_original[3];
    int large_leaf;
};

static int is_page_aligned(uint64_t value) {
    return (value & (VM_PAGE_SIZE - 1)) == 0;
}

static int is_low_canonical(uint64_t va) {
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
    for (size_t i = 0; i < VM_TABLE_COUNT; ++i) {
        if (table_in_use[i] &&
            ((uint64_t)(uintptr_t)table_pool[i] & VM_ADDR_MASK) == address)
            return table_pool[i];
    }
    return 0;
}

static uint64_t *table_from_physical(uint64_t physical)
{
    if ((physical & (VM_PAGE_SIZE - 1U)) != 0)
        return 0;
    return entry_table(physical);
}

static int address_space_root(const vm_address_space_t *space,
                              uint64_t **out_root)
{
    uint64_t *root;
    uint64_t *pdpt;

    if (space == 0 || out_root == 0 ||
        space->magic != VM_ASPACE_MAGIC || space->reserved != 0)
        return VM_ERR_ARGUMENT;
    root = table_from_physical(space->root_physical);
    pdpt = table_from_physical(space->pdpt_physical);
    if (root == 0 || pdpt == 0 ||
        (root[0] & VM_ADDR_MASK) !=
            (space->pdpt_physical & VM_ADDR_MASK))
        return VM_ERR_CORRUPT;
    *out_root = root;
    return VM_OK;
}

static int record_created(struct walk_path *path, uint64_t *parent,
                          unsigned int index, uint64_t *table,
                          uint64_t original) {
    if (path->created_count >= 3) return VM_ERR_CORRUPT;
    size_t n = path->created_count++;
    path->created[n] = table;
    path->created_parent[n] = parent;
    path->created_index[n] = index;
    path->created_original[n] = original;
    return VM_OK;
}

static void rollback_created(struct walk_path *path) {
    while (path->promoted_count != 0) {
        size_t n = --path->promoted_count;
        *path->promoted_entries[n] = path->promoted_original[n];
    }

    while (path->created_count != 0) {
        size_t n = --path->created_count;
        path->created_parent[n][path->created_index[n]] =
            path->created_original[n];
        release_table(path->created[n]);
    }
}

/*
 * Split one 2 MiB PDE into 512 4 KiB PTEs.
 * Existing mappings retain their physical addresses and leaf permissions.
 * The target mapping may then receive its own independent U/S and NX flags.
 */
static int split_large_page(uint64_t *parent, unsigned int index,
                            uint64_t old_entry, uint64_t requested_flags,
                            struct walk_path *path) {
    uint64_t *pt = allocate_table();
    if (!pt) return VM_ERR_NO_MEMORY;

    uint64_t physical_base = old_entry & VM_LARGE_ADDR_MASK;
    uint64_t inherited = old_entry & VM_LEAF_FLAGS;

    /* Large-page PAT (bit 12) becomes 4 KiB PTE PAT (bit 7). */
    if (old_entry & (1ULL << 12)) inherited |= (1ULL << 7);

    for (size_t i = 0; i < VM_ENTRIES; ++i) {
        pt[i] = (physical_base + (uint64_t)i * VM_PAGE_SIZE) | inherited;
    }

    /*
     * A parent entry controls the page-table page, not the mapped data.
     * Keep U/S and A; make the parent writable so individual PTEs can
     * enforce read-only pages. NX is kept only when the requested leaf is NX;
     * inherited NX remains on all unaffected PTEs.
     */
    uint64_t parent_flags = VM_PRESENT | VM_WRITE |
                            (old_entry & (VM_USER | VM_ACCESSED));
    if ((old_entry & VM_NO_EXECUTE) &&
        (requested_flags & VM_NO_EXECUTE))
        parent_flags |= VM_NO_EXECUTE;
    if (requested_flags & VM_USER) parent_flags |= VM_USER;

    int rc = record_created(path, parent, index, pt, old_entry);
    if (rc != VM_OK) {
        release_table(pt);
        return rc;
    }

    parent[index] = ((uint64_t)(uintptr_t)pt & VM_ADDR_MASK) | parent_flags;
    return VM_OK;
}

static int walk(uint64_t *root, uint64_t va, int create, uint64_t flags,
                struct walk_path *path, uint64_t **leaf_entry) {
    static const unsigned int shifts[3] = {39, 30, 21};
    uint64_t *table = root;

    if (root == 0 || path == 0 || leaf_entry == 0)
        return VM_ERR_ARGUMENT;
    path->created_count = 0;
    path->promoted_count = 0;
    path->large_leaf = 0;
    path->tables[0] = root;

    for (size_t level = 0; level < 3; ++level) {
        unsigned int index = page_index(va, shifts[level]);
        uint64_t entry = table[index];
        path->tables[level] = table;
        path->indices[level] = index;

        if (!(entry & VM_PRESENT)) {
            if (!create) return VM_ERR_NOT_MAPPED;
            uint64_t *next = allocate_table();
            if (!next) return VM_ERR_NO_MEMORY;
            int rc = record_created(path, table, index, next, 0);
            if (rc != VM_OK) {
                release_table(next);
                return rc;
            }
            entry = ((uint64_t)(uintptr_t)next & VM_ADDR_MASK) |
                    VM_PRESENT | VM_WRITE | (flags & VM_USER);
            table[index] = entry;
        } else if (entry & VM_LARGE_PAGE) {
            if (level != 2) return VM_ERR_CORRUPT;
            if (!create) {
                path->large_leaf = 1;
                *leaf_entry = &table[index];
                return VM_OK;
            }
            int rc = split_large_page(table, index, entry, flags, path);
            if (rc != VM_OK) return rc;
            entry = table[index];
        } else if ((flags & VM_USER) && !(entry & VM_USER)) {
            if (path->promoted_count >= 3) return VM_ERR_CORRUPT;
            size_t n = path->promoted_count++;
            path->promoted_entries[n] = &table[index];
            path->promoted_original[n] = entry;
            entry |= VM_USER;
            table[index] = entry;
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
    /*
     * Never destroy page tables while CR3 may still reference them.
     * A reboot or a dedicated deactivation path is required first.
     */
    if (active_root != 0)
        return VM_ERR_ACTIVE;

    for (size_t i = 0; i < VM_TABLE_COUNT; ++i) {
        zero_table(table_pool[i]);
        table_in_use[i] = 0;
    }
    tables_used = 0;
    root_table = 0;
    active_root = 0;

    uint64_t *root = allocate_table();
    uint64_t *pdpt = allocate_table();
    uint64_t *pd = allocate_table();
    if (!root || !pdpt || !pd) {
        if (pd) release_table(pd);
        if (pdpt) release_table(pdpt);
        if (root) release_table(root);
        return VM_ERR_NO_MEMORY;
    }

    root_table = root;
    root[0] = ((uint64_t)(uintptr_t)pdpt & VM_ADDR_MASK) |
              VM_PRESENT | VM_WRITE;
    pdpt[0] = ((uint64_t)(uintptr_t)pd & VM_ADDR_MASK) |
              VM_PRESENT | VM_WRITE;

    /* Match boot.S: supervisor identity-map the first 1 GiB with 2 MiB pages. */
    for (size_t i = 0; i < VM_ENTRIES; ++i) {
        pd[i] = ((uint64_t)i * VM_LARGE_SIZE) |
                VM_PRESENT | VM_WRITE | VM_LARGE_PAGE;
    }
    return VM_OK;
}

int vm_map_page(uint64_t va, uint64_t pa, uint64_t flags) {
    struct walk_path path;
    uint64_t *pte = 0;
    if (!root_table) return VM_ERR_ARGUMENT;
    if (!is_low_canonical(va)) return VM_ERR_ADDRESS;
    if (!is_page_aligned(va) || !is_page_aligned(pa))
        return VM_ERR_ALIGNMENT;
    if (!is_valid_physical_page(pa)) return VM_ERR_ADDRESS;
    if (flags & ~VM_LEAF_FLAG_MASK) return VM_ERR_FLAGS;

    /* VM_USER_WX_GLOBAL_GUARD: user mappings must be W^X and non-global. */
    if ((flags & (VM_USER | VM_WRITE)) == (VM_USER | VM_WRITE) &&
        !(flags & VM_NO_EXECUTE))
        return VM_ERR_FLAGS;
    if ((flags & (VM_USER | VM_GLOBAL)) == (VM_USER | VM_GLOBAL))
        return VM_ERR_FLAGS;

    int status = walk(root_table, va, 1, flags, &path, &pte);
    if (status != VM_OK) {
        rollback_created(&path);
        return status;
    }
    if (*pte & VM_PRESENT) {
        rollback_created(&path);
        return VM_ERR_EXISTS;
    }
    *pte = pa | VM_PRESENT | flags;

    /*
     * If this address was cached as non-present, or had an older
     * translation, discard that translation after changing the PTE.
     */
    if (active_root == root_table) {
        status = vm_invalidate_page(va);
        if (status != VM_OK)
            return status;
    }

    return VM_OK;
}

int vm_unmap_page(uint64_t va) {
    struct walk_path path;
    uint64_t *pte = 0;
    if (!root_table) return VM_ERR_ARGUMENT;
    if (!is_low_canonical(va)) return VM_ERR_ADDRESS;
    if (!is_page_aligned(va)) return VM_ERR_ALIGNMENT;

    /*
     * create=1 deliberately splits a 2 MiB mapping when unmapping one 4 KiB
     * page from it. Any newly-created empty path is rolled back on failure.
     */
    int status = walk(root_table, va, 1, 0, &path, &pte);
    if (status != VM_OK) {
        rollback_created(&path);
        return status;
    }
    if (!(*pte & VM_PRESENT)) {
        rollback_created(&path);
        return VM_ERR_NOT_MAPPED;
    }

    *pte = 0;
    for (int level = 3; level >= 1; --level) {
        uint64_t *child = path.tables[level];
        uint64_t *parent = path.tables[level - 1];
        unsigned int parent_index = path.indices[level - 1];
        if (!table_is_empty(child)) break;
        parent[parent_index] = 0;
        release_table(child);
    }

    if (active_root == root_table) {
        status = vm_invalidate_page(va);
        if (status != VM_OK)
            return status;
    }

    return VM_OK;
}

int vm_query_page(uint64_t va, uint64_t *pa, uint64_t *flags) {
    struct walk_path path;
    uint64_t *entry_ptr = 0;
    if (!root_table || !pa || !flags) return VM_ERR_ARGUMENT;
    if (!is_low_canonical(va)) return VM_ERR_ADDRESS;

    int status = walk(root_table, va, 0, 0, &path, &entry_ptr);
    if (status != VM_OK) return status;

    uint64_t entry = *entry_ptr;
    if (!(entry & VM_PRESENT)) return VM_ERR_NOT_MAPPED;

    if (path.large_leaf) {
        *pa = (entry & VM_LARGE_ADDR_MASK) | (va & (VM_LARGE_SIZE - 1));
        *flags = entry & (VM_PRESENT | VM_WRITE | VM_USER | VM_PWT |
                          VM_PCD | VM_ACCESSED | VM_DIRTY |
                          VM_LARGE_PAGE | VM_GLOBAL | VM_NO_EXECUTE |
                          (1ULL << 12));
    } else {
        *pa = (entry & VM_ADDR_MASK) | (va & (VM_PAGE_SIZE - 1));
        *flags = entry & ~VM_ADDR_MASK;
    }
    return VM_OK;
}

uint64_t vm_root_physical(void) {
    return root_table ? (uint64_t)(uintptr_t)root_table : 0;
}

size_t vm_tables_used(void) { return tables_used; }
size_t vm_tables_capacity(void) { return VM_TABLE_COUNT; }

int vm_self_test(void) {
    const uint64_t test_va = 0x0000000040000000ULL;
    const uint64_t test_pa = 0x0000000030000000ULL;
    const uint64_t test_flags = VM_WRITE | VM_NO_EXECUTE;
    const uint64_t user_va = 0x0000000080000000ULL;
    const uint64_t user_pa = 0x0000000020000000ULL;
    const uint64_t split_va = 0x0000000002000000ULL;
    const uint64_t split_pa = 0x0000000003000000ULL;
    uint64_t queried_pa = 0;
    uint64_t queried_flags = 0;

    if (vm_init() != VM_OK) return -1;
    if (vm_tables_used() != 3) return -2;

    /* The bootstrap identity map must be queryable as a 2 MiB large page. */
    if (vm_query_page(0x02001234ULL, &queried_pa, &queried_flags) != VM_OK)
        return -3;
    if (queried_pa != 0x02001234ULL ||
        !(queried_flags & VM_LARGE_PAGE) ||
        (queried_flags & VM_USER)) return -4;

    if (vm_map_page(test_va, test_pa, test_flags) != VM_OK) return -5;
    if (vm_query_page(test_va + 0x123, &queried_pa, &queried_flags) != VM_OK)
        return -6;
    if (queried_pa != test_pa + 0x123) return -7;
    if ((queried_flags & (VM_PRESENT | VM_WRITE | VM_NO_EXECUTE)) !=
        (VM_PRESENT | VM_WRITE | VM_NO_EXECUTE)) return -8;

    if (vm_map_page(test_va, test_pa, test_flags) != VM_ERR_EXISTS) return -9;
    if (vm_map_page(test_va + 1, test_pa, test_flags) != VM_ERR_ALIGNMENT)
        return -10;
    if (vm_map_page(test_va, test_pa + 1, test_flags) != VM_ERR_ALIGNMENT)
        return -11;
    if (vm_map_page(VM_LOW_CANONICAL_LIMIT, test_pa, test_flags) != VM_ERR_ADDRESS)
        return -12;
    if (vm_map_page(test_va + VM_PAGE_SIZE, test_pa, VM_PRESENT) != VM_ERR_FLAGS)
        return -13;

    if (vm_map_page(user_va, user_pa, VM_USER | VM_WRITE | VM_NO_EXECUTE)
        != VM_OK) return -14;
    if (vm_query_page(user_va, &queried_pa, &queried_flags) != VM_OK)
        return -15;
    if (queried_pa != user_pa) return -16;
    if ((queried_flags & (VM_USER | VM_WRITE | VM_NO_EXECUTE)) !=
        (VM_USER | VM_WRITE | VM_NO_EXECUTE)) return -17;

    /* Promoting upper levels must not make a supervisor leaf user-accessible. */
    if (vm_query_page(test_va, &queried_pa, &queried_flags) != VM_OK)
        return -18;
    if (queried_pa != test_pa || (queried_flags & VM_USER)) return -19;

    /* VM_USER_WX_SELFTEST: reject writable+executable user pages. */
    if (vm_map_page(0x0000000040100000ULL, 0x0000000030100000ULL,
                    VM_USER | VM_WRITE) != VM_ERR_FLAGS) return -45;
    /* User mappings must never survive a CR3 switch as global TLB entries. */
    if (vm_map_page(0x0000000040100000ULL, 0x0000000030100000ULL,
                    VM_USER | VM_NO_EXECUTE | VM_GLOBAL) != VM_ERR_FLAGS)
        return -46;
    if (vm_map_page(0x0000000040100000ULL, 0x0000000030100000ULL,
                    VM_USER | VM_WRITE | VM_NO_EXECUTE) != VM_OK) return -20;
    if (vm_query_page(0x0000000040100000ULL,
                      &queried_pa, &queried_flags) != VM_OK) return -21;
    if (!(queried_flags & VM_USER)) return -22;
    if (!(queried_flags & VM_NO_EXECUTE)) return -47;
    if (vm_query_page(test_va, &queried_pa, &queried_flags) != VM_OK ||
        (queried_flags & VM_USER)) return -23;

    /* Split one supervisor 2 MiB page, then grant USER to one 4 KiB PTE. */
    if (vm_unmap_page(split_va) != VM_OK) return -24;
    /* VM_USER_NX_SELFTEST_MIGRATION_20261121 */
    if (vm_map_page(split_va, split_pa,
                   VM_USER | VM_WRITE | VM_NO_EXECUTE) != VM_OK)
        return -25;
    if (vm_query_page(split_va, &queried_pa, &queried_flags) != VM_OK ||
        queried_pa != split_pa || !(queried_flags & VM_USER)) return -26;
    if (vm_query_page(split_va + VM_PAGE_SIZE, &queried_pa, &queried_flags) != VM_OK ||
        queried_pa != split_va + VM_PAGE_SIZE || (queried_flags & VM_USER))
        return -27;
    if (vm_unmap_page(split_va) != VM_OK) return -28;
    if (vm_query_page(split_va, &queried_pa, &queried_flags) != VM_ERR_NOT_MAPPED)
        return -29;

    if (vm_unmap_page(test_va) != VM_OK) return -30;
    if (vm_query_page(test_va, &queried_pa, &queried_flags) != VM_ERR_NOT_MAPPED)
        return -31;

    if (vm_query_page(user_va + 0x321, &queried_pa, &queried_flags) != VM_OK)
        return -32;
    if (queried_pa != user_pa + 0x321) return -33;

    /* Exhaust the pool, then prove failed USER promotion is rolled back. */
    if (vm_init() != VM_OK) return -34;
    {
        const uint64_t base_va = 0x0000000040000000ULL;
        const uint64_t base_pa = 0x0000000010000000ULL;
        const size_t mappings = VM_TABLE_COUNT - 4U;
        uint64_t *pdpt;
        int status;

        for (size_t i = 0; i < mappings; ++i) {
            uint64_t va = base_va + (uint64_t)i * VM_LARGE_SIZE;
            uint64_t pa = base_pa + (uint64_t)i * VM_PAGE_SIZE;
            status = vm_map_page(va, pa, VM_WRITE | VM_NO_EXECUTE);
            if (status != VM_OK) return -35;
        }
        if (vm_tables_used() != vm_tables_capacity()) return -36;

        uint64_t target_va = base_va + (uint64_t)mappings * VM_LARGE_SIZE;
        status = vm_map_page(target_va, base_pa + 0x1000000ULL,
                             VM_USER | VM_WRITE | VM_NO_EXECUTE);
        if (status != VM_ERR_NO_MEMORY) return -37;
        if (vm_tables_used() != vm_tables_capacity()) return -38;
        if (vm_query_page(target_va, &queried_pa, &queried_flags) !=
            VM_ERR_NOT_MAPPED) return -39;

        if (root_table[0] & VM_USER) return -40;
        pdpt = entry_table(root_table[0]);
        if (!pdpt || (pdpt[1] & VM_USER)) return -41;

        if (vm_query_page(base_va, &queried_pa, &queried_flags) != VM_OK ||
            queried_pa != base_pa) return -42;
        for (size_t i = 0; i < mappings; ++i) {
            uint64_t va = base_va + (uint64_t)i * VM_LARGE_SIZE;
            if (vm_unmap_page(va) != VM_OK) return -43;
        }
        if (vm_tables_used() != 3) return -44;
    }

    return VM_OK;
}


/*
 * Runtime CR3 management.
 * The page-table root is allocated from the kernel's static table pool.
 * The bootstrap identity map must remain available while this code runs.
 */
int vm_is_active(void)
{
    return active_root != 0;
}

int vm_invalidate_page(uint64_t virtual_address)
{
    if (!is_low_canonical(virtual_address))
        return VM_ERR_ADDRESS;

    if (active_root == 0)
        return VM_OK;

#ifdef VM_HOST_TEST
    return VM_OK;
#else
    __asm__ volatile("invlpg (%0)" :: "r"(virtual_address) : "memory");
    return VM_OK;
#endif
}

static int activate_root(uint64_t *root_table_pointer)
{
    uint64_t root = (uint64_t)(uintptr_t)root_table_pointer;
    if (root_table_pointer == 0 || (root & (VM_PAGE_SIZE - 1U)) != 0)
        return VM_ERR_CORRUPT;
#ifdef VM_HOST_TEST
    active_root = root_table_pointer;
    return VM_OK;
#else
    if (root >= (1ULL << 30))
        return VM_ERR_ADDRESS;
    uint64_t current_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(current_cr3));
    if ((current_cr3 & VM_ADDR_MASK) != (root & VM_ADDR_MASK)) {
        __asm__ volatile("mov %0, %%cr3" : : "r"(root) : "memory");
    }
    active_root = root_table_pointer;
    return VM_OK;
#endif
}

int vm_activate(void)
{
    if (root_table == 0)
        return VM_ERR_ARGUMENT;
    return activate_root(root_table);
}

uint64_t vm_current_root_physical(void)
{
    return active_root ? (uint64_t)(uintptr_t)active_root : 0;
}


static int map_page_in_root(uint64_t *root, uint64_t va, uint64_t pa,
                            uint64_t flags)
{
    struct walk_path path;
    uint64_t *pte = 0;
    int status;

    if (root == 0) return VM_ERR_ARGUMENT;
    if (!is_low_canonical(va)) return VM_ERR_ADDRESS;
    if (!is_page_aligned(va) || !is_page_aligned(pa))
        return VM_ERR_ALIGNMENT;
    if (!is_valid_physical_page(pa)) return VM_ERR_ADDRESS;
    if (flags & ~VM_LEAF_FLAG_MASK) return VM_ERR_FLAGS;
    if ((flags & (VM_USER | VM_WRITE)) == (VM_USER | VM_WRITE) &&
        !(flags & VM_NO_EXECUTE))
        return VM_ERR_FLAGS;
    if ((flags & (VM_USER | VM_GLOBAL)) == (VM_USER | VM_GLOBAL))
        return VM_ERR_FLAGS;

    status = walk(root, va, 1, flags, &path, &pte);
    if (status != VM_OK) {
        rollback_created(&path);
        return status;
    }
    if (*pte & VM_PRESENT) {
        rollback_created(&path);
        return VM_ERR_EXISTS;
    }
    *pte = pa | VM_PRESENT | flags;
    if (active_root == root)
        return vm_invalidate_page(va);
    return VM_OK;
}

static int protect_page_in_root(uint64_t *root, uint64_t va, uint64_t flags)
{
    struct walk_path path;
    uint64_t *pte = 0;
    uint64_t old_entry;
    int status;

    if (root == 0) return VM_ERR_ARGUMENT;
    if (!is_low_canonical(va)) return VM_ERR_ADDRESS;
    if (!is_page_aligned(va)) return VM_ERR_ALIGNMENT;
    if (flags & ~VM_LEAF_FLAG_MASK) return VM_ERR_FLAGS;
    if ((flags & (VM_USER | VM_WRITE)) == (VM_USER | VM_WRITE) &&
        !(flags & VM_NO_EXECUTE))
        return VM_ERR_FLAGS;
    if ((flags & (VM_USER | VM_GLOBAL)) == (VM_USER | VM_GLOBAL))
        return VM_ERR_FLAGS;

    status = walk(root, va, 0, 0, &path, &pte);
    if (status != VM_OK) return status;
    if (path.large_leaf || !(*pte & VM_PRESENT))
        return VM_ERR_NOT_MAPPED;
    old_entry = *pte;
    *pte = (old_entry & VM_ADDR_MASK) | VM_PRESENT | flags;
    if (active_root == root)
        return vm_invalidate_page(va);
    return VM_OK;
}

static int unmap_page_in_root(uint64_t *root, uint64_t va)
{
    struct walk_path path;
    uint64_t *pte = 0;
    int status;

    if (root == 0) return VM_ERR_ARGUMENT;
    if (!is_low_canonical(va)) return VM_ERR_ADDRESS;
    if (!is_page_aligned(va)) return VM_ERR_ALIGNMENT;
    status = walk(root, va, 1, 0, &path, &pte);
    if (status != VM_OK) {
        rollback_created(&path);
        return status;
    }
    if (!(*pte & VM_PRESENT)) {
        rollback_created(&path);
        return VM_ERR_NOT_MAPPED;
    }
    *pte = 0;
    for (int level = 3; level >= 1; --level) {
        uint64_t *child = path.tables[level];
        uint64_t *parent = path.tables[level - 1];
        unsigned int parent_index = path.indices[level - 1];
        if (!table_is_empty(child)) break;
        parent[parent_index] = 0;
        release_table(child);
    }
    if (active_root == root)
        return vm_invalidate_page(va);
    return VM_OK;
}

static int query_page_in_root(uint64_t *root, uint64_t va,
                              uint64_t *pa, uint64_t *flags)
{
    struct walk_path path;
    uint64_t *entry_ptr = 0;
    uint64_t entry;
    int status;

    if (root == 0 || pa == 0 || flags == 0)
        return VM_ERR_ARGUMENT;
    if (!is_low_canonical(va)) return VM_ERR_ADDRESS;
    status = walk(root, va, 0, 0, &path, &entry_ptr);
    if (status != VM_OK) return status;
    entry = *entry_ptr;
    if (!(entry & VM_PRESENT)) return VM_ERR_NOT_MAPPED;
    if (path.large_leaf) {
        *pa = (entry & VM_LARGE_ADDR_MASK) | (va & (VM_LARGE_SIZE - 1U));
        *flags = entry & (VM_PRESENT | VM_WRITE | VM_USER | VM_PWT |
                          VM_PCD | VM_ACCESSED | VM_DIRTY | VM_LARGE_PAGE |
                          VM_GLOBAL | VM_NO_EXECUTE | (1ULL << 12));
    } else {
        *pa = (entry & VM_ADDR_MASK) | (va & (VM_PAGE_SIZE - 1U));
        *flags = entry & ~VM_ADDR_MASK;
    }
    return VM_OK;
}

int vm_address_space_create(vm_address_space_t *space)
{
    uint64_t *root;
    uint64_t *pdpt;
    uint64_t *kernel_pdpt;

    if (space == 0 || root_table == 0)
        return VM_ERR_ARGUMENT;

    root = allocate_table();
    if (root == 0) return VM_ERR_NO_MEMORY;
    pdpt = allocate_table();
    if (pdpt == 0) {
        release_table(root);
        return VM_ERR_NO_MEMORY;
    }
    kernel_pdpt = entry_table(root_table[0]);
    if (kernel_pdpt == 0 || !(kernel_pdpt[0] & VM_PRESENT)) {
        release_table(pdpt);
        release_table(root);
        return VM_ERR_CORRUPT;
    }

    /* Share only the supervisor-only first-GiB kernel identity-map PD. */
    pdpt[0] = kernel_pdpt[0];
    root[0] = ((uint64_t)(uintptr_t)pdpt & VM_ADDR_MASK) |
              VM_PRESENT | VM_WRITE;
    space->root_physical = (uint64_t)(uintptr_t)root;
    space->pdpt_physical = (uint64_t)(uintptr_t)pdpt;
    space->magic = VM_ASPACE_MAGIC;
    space->reserved = 0;
    return VM_OK;
}

static int validate_pdpt_children(const uint64_t *pdpt,
                                  int preserve_kernel_pd)
{
    for (unsigned int i = 0; i < VM_ENTRIES; ++i) {
        uint64_t pdpte = pdpt[i];
        uint64_t *pd;
        if (!(pdpte & VM_PRESENT)) continue;
        if (preserve_kernel_pd && i == 0) continue;
        if (pdpte & VM_LARGE_PAGE) continue;
        pd = entry_table(pdpte);
        if (pd == 0) return VM_ERR_CORRUPT;
        for (unsigned int j = 0; j < VM_ENTRIES; ++j) {
            uint64_t pde = pd[j];
            if (!(pde & VM_PRESENT) || (pde & VM_LARGE_PAGE)) continue;
            if (entry_table(pde) == 0) return VM_ERR_CORRUPT;
        }
    }
    return VM_OK;
}

static void release_pdpt_children(uint64_t *pdpt, int preserve_kernel_pd)
{
    for (unsigned int i = 0; i < VM_ENTRIES; ++i) {
        uint64_t pdpte = pdpt[i];
        uint64_t *pd;
        if (!(pdpte & VM_PRESENT)) continue;
        if (preserve_kernel_pd && i == 0) continue;
        if (pdpte & VM_LARGE_PAGE) continue;
        pd = entry_table(pdpte);
        if (pd == 0) continue; /* Preflight has already validated the tree. */
        for (unsigned int j = 0; j < VM_ENTRIES; ++j) {
            uint64_t pde = pd[j];
            uint64_t *pt;
            if (!(pde & VM_PRESENT) || (pde & VM_LARGE_PAGE)) continue;
            pt = entry_table(pde);
            if (pt != 0) release_table(pt);
        }
        release_table(pd);
    }
}

int vm_address_space_destroy(vm_address_space_t *space)
{
    uint64_t *root;
    uint64_t *pdpt;
    int status;

    status = address_space_root(space, &root);
    if (status != VM_OK) return status;
    if (active_root == root) return VM_ERR_ACTIVE;
    pdpt = table_from_physical(space->pdpt_physical);
    if (pdpt == 0) return VM_ERR_CORRUPT;

    /* Preflight the complete private subtree before releasing any table. */
    status = validate_pdpt_children(pdpt, 1);
    if (status != VM_OK) return status;
    for (unsigned int i = 1; i < VM_ENTRIES; ++i) {
        uint64_t pml4e = root[i];
        uint64_t *other_pdpt;
        if (!(pml4e & VM_PRESENT)) continue;
        if (pml4e & VM_LARGE_PAGE) return VM_ERR_CORRUPT;
        other_pdpt = entry_table(pml4e);
        if (other_pdpt == 0) return VM_ERR_CORRUPT;
        status = validate_pdpt_children(other_pdpt, 0);
        if (status != VM_OK) return status;
    }

    release_pdpt_children(pdpt, 1);
    for (unsigned int i = 1; i < VM_ENTRIES; ++i) {
        uint64_t pml4e = root[i];
        uint64_t *other_pdpt;
        if (!(pml4e & VM_PRESENT)) continue;
        other_pdpt = entry_table(pml4e);
        release_pdpt_children(other_pdpt, 0);
        release_table(other_pdpt);
    }

    release_table(pdpt);
    release_table(root);
    space->root_physical = 0;
    space->pdpt_physical = 0;
    space->magic = 0;
    space->reserved = 0;
    return VM_OK;
}

int vm_address_space_activate(const vm_address_space_t *space)
{
    uint64_t *root;
    int status = address_space_root(space, &root);
    if (status != VM_OK) return status;
    return activate_root(root);
}

int vm_address_space_map_page(vm_address_space_t *space, uint64_t va,
                              uint64_t pa, uint64_t flags)
{
    uint64_t *root;
    int status = address_space_root(space, &root);
    if (status != VM_OK) return status;
    if (va < VM_USER_MIN_VA || va >= VM_USER_MAX_VA)
        return VM_ERR_ADDRESS;
    if (!(flags & VM_USER))
        return VM_ERR_FLAGS;
    return map_page_in_root(root, va, pa, flags);
}

int vm_address_space_unmap_page(vm_address_space_t *space, uint64_t va)
{
    uint64_t *root;
    int status = address_space_root(space, &root);
    if (status != VM_OK) return status;
    if (va < VM_USER_MIN_VA || va >= VM_USER_MAX_VA)
        return VM_ERR_ADDRESS;
    return unmap_page_in_root(root, va);
}

int vm_address_space_protect_page(vm_address_space_t *space, uint64_t va,
                                  uint64_t flags)
{
    uint64_t *root;
    int status = address_space_root(space, &root);
    if (status != VM_OK) return status;
    if (va < VM_USER_MIN_VA || va >= VM_USER_MAX_VA)
        return VM_ERR_ADDRESS;
    if (!(flags & VM_USER))
        return VM_ERR_FLAGS;
    return protect_page_in_root(root, va, flags);
}

int vm_address_space_query_page(const vm_address_space_t *space, uint64_t va,
                                uint64_t *pa, uint64_t *flags)
{
    uint64_t *root;
    int status = address_space_root(space, &root);
    if (status != VM_OK) return status;
    return query_page_in_root(root, va, pa, flags);
}

/*
 * Return true only when both CPU NX support and EFER.NXE
 * are enabled. boot.S enables EFER.NXE when supported.
 */
int vm_nx_enabled(void)
{
#ifdef VM_HOST_TEST
    /*
     * Host tests validate page-table bookkeeping only.
     * They must not execute privileged x86 instructions.
     * NX capability is checked by the real kernel build.
     */
    return 0;
#else
    uint32_t a, b, c, d;

    a = 0x80000000U;
    __asm__ volatile(
        "cpuid"
        : "+a"(a), "=b"(b), "=c"(c), "=d"(d)
        :
        : "memory"
    );

    if (a < 0x80000001U)
        return 0;

    a = 0x80000001U;
    __asm__ volatile(
        "cpuid"
        : "+a"(a), "=b"(b), "=c"(c), "=d"(d)
        :
        : "memory"
    );

    if (!(d & (1U << 20)))
        return 0;

    a = 0xC0000080U;
    __asm__ volatile(
        "rdmsr"
        : "=a"(a), "=d"(d)
        : "c"(a)
        : "memory"
    );

    return (a & (1U << 11)) != 0;
#endif
}
