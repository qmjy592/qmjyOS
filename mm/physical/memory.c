#include "memory.h"
#include <stddef.h>

/* Conservative early allocator: only type-1 Multiboot ranges below 1 GiB,
 * because the current bootstrap identity maps only that physical window. */
#define MM_LIMIT (1ULL << 30)
#define MM_MAX_PAGES 262144U /* 1 GiB / 4 KiB; bitmap is 32 KiB. */
#define MM_WORD_BITS 64U

static uint64_t usable[MM_MAX_RANGES];
static uint64_t usable_len[MM_MAX_RANGES];
static size_t ranges;

/* Non-usable Multiboot ranges are tracked separately from caller reservations. */
static uint64_t firmware_reserved_base[MM_MAX_RANGES];
static uint64_t firmware_reserved_len[MM_MAX_RANGES];
static size_t firmware_reserved_count;

/* Caller-owned reservations survive a new allocator initialization. Boot-owned
 * reservations are rebuilt from the new Multiboot info on each init attempt. */
static uint64_t reserved_base[MM_MAX_RESERVED];
static uint64_t reserved_len[MM_MAX_RESERVED];
static uint8_t reserved_is_boot[MM_MAX_RESERVED];
static size_t reserved_count;

static uint64_t page_bitmap[MM_MAX_PAGES / MM_WORD_BITS];
static size_t total_pages, free_count;
static int initialized;

static int align_up_checked(uint64_t x, uint64_t *out) {
    if (!out || x > UINT64_MAX - (MM_PAGE_SIZE - 1)) return 0;
    *out = (x + MM_PAGE_SIZE - 1) & ~(MM_PAGE_SIZE - 1);
    return 1;
}

static uint64_t align_down(uint64_t x) {
    return x & ~(MM_PAGE_SIZE - 1);
}

static int overlap(uint64_t a, uint64_t al, uint64_t b, uint64_t bl) {
    if (!al || !bl) return 0;
    if (a > UINT64_MAX - al || b > UINT64_MAX - bl) return 1;
    return a < b + bl && b < a + al;
}

static int bit_get(size_t p) {
    return (int)((page_bitmap[p / MM_WORD_BITS] >> (p % MM_WORD_BITS)) & 1U);
}

static void bit_set(size_t p) {
    page_bitmap[p / MM_WORD_BITS] |= 1ULL << (p % MM_WORD_BITS);
}

static void bit_clear(size_t p) {
    page_bitmap[p / MM_WORD_BITS] &= ~(1ULL << (p % MM_WORD_BITS));
}

/* Leave the allocator in a safe, unusable state after any parse/init failure.
 * Reservation records are kept by this helper; callers decide which records
 * belong to the failed boot attempt and should be rolled back. */
static void allocator_disable(void) {
    initialized = 0;
    ranges = 0;
    total_pages = 0;
    free_count = 0;
    for (size_t i = 0; i < MM_MAX_PAGES / MM_WORD_BITS; ++i)
        page_bitmap[i] = UINT64_MAX;
}

static int range_is_reserved(uint64_t pa) {
    for (size_t i = 0; i < reserved_count; ++i) {
        if (overlap(pa, MM_PAGE_SIZE, reserved_base[i], reserved_len[i])) return 1;
    }
    for (size_t i = 0; i < firmware_reserved_count; ++i) {
        if (overlap(pa, MM_PAGE_SIZE,
                    firmware_reserved_base[i], firmware_reserved_len[i])) return 1;
    }
    return 0;
}

static int reservation_exists(uint64_t base, uint64_t length) {
    for (size_t i = 0; i < reserved_count; ++i) {
        if (reserved_base[i] == base && reserved_len[i] == length) return 1;
    }
    return 0;
}

static void remove_boot_reservations(void) {
    size_t out = 0;
    for (size_t i = 0; i < reserved_count; ++i) {
        if (reserved_is_boot[i]) continue;
        if (out != i) {
            reserved_base[out] = reserved_base[i];
            reserved_len[out] = reserved_len[i];
            reserved_is_boot[out] = 0;
        }
        ++out;
    }
    reserved_count = out;
}

/* Internal variant tags reservations derived from Multiboot so reinitialization
 * can replace stale boot metadata without discarding caller-owned reservations. */
static int mm_reserve_internal(uint64_t base, uint64_t length, int boot_owned) {
    if (!length) return MM_OK;
    if (base > UINT64_MAX - length) return MM_ERR_ARGUMENT;
    if (reservation_exists(base, length)) return MM_OK;
    if (reserved_count >= MM_MAX_RESERVED) return MM_ERR_CAPACITY;

    uint64_t end = base + length;
    uint64_t lo = align_down(base);
    uint64_t hi;
    if (end >= MM_LIMIT) hi = MM_LIMIT;
    else if (!align_up_checked(end, &hi)) hi = MM_LIMIT;
    if (lo > MM_LIMIT) lo = MM_LIMIT;

    /* Once active, reserving an allocated (rather than free or already
     * reserved) page would silently invalidate a live allocation. Reject it. */
    if (initialized) {
        for (size_t r = 0; r < ranges; ++r) {
            uint64_t r_end = usable[r] + usable_len[r];
            uint64_t start = lo > usable[r] ? lo : usable[r];
            uint64_t stop = hi < r_end ? hi : r_end;
            for (uint64_t pa = start; pa < stop; pa += MM_PAGE_SIZE) {
                size_t p = (size_t)(pa / MM_PAGE_SIZE);
                if (bit_get(p) && !range_is_reserved(pa)) return MM_ERR_ARGUMENT;
            }
        }
    }

    reserved_base[reserved_count] = base;
    reserved_len[reserved_count] = length;
    reserved_is_boot[reserved_count] = (uint8_t)(boot_owned != 0);
    ++reserved_count;

    if (initialized) {
        for (size_t r = 0; r < ranges; ++r) {
            uint64_t r_end = usable[r] + usable_len[r];
            uint64_t start = lo > usable[r] ? lo : usable[r];
            uint64_t stop = hi < r_end ? hi : r_end;
            for (uint64_t pa = start; pa < stop; pa += MM_PAGE_SIZE) {
                size_t p = (size_t)(pa / MM_PAGE_SIZE);
                if (!bit_get(p)) {
                    bit_set(p);
                    if (free_count) --free_count;
                    if (total_pages) --total_pages;
                }
            }
        }
    }
    return MM_OK;
}

int mm_reserve(uint64_t base, uint64_t length) {
    return mm_reserve_internal(base, length, 0);
}

static int mm_parse_boot_regions(const void *input,
                                 size_t count,
                                 size_t entry_size)
{
    const uint8_t *bytes = (const uint8_t *)input;
    int error = MM_ERR_FORMAT;

    allocator_disable();
    firmware_reserved_count = 0;

    if (input == NULL || count == 0 || count > MM_MAX_RANGES ||
        entry_size < sizeof(qm_boot_memory_region_t) ||
        count > (size_t)-1 / entry_size)
        return MM_ERR_ARGUMENT;

    for (size_t i = 0; i < count; ++i) {
        qm_boot_memory_region_t region;
        uint64_t base;
        uint64_t length;
        uint64_t end;

        __builtin_memcpy(&region, bytes + i * entry_size, sizeof(region));
        base = region.base;
        length = region.length;

        if (length == 0 || base > UINT64_MAX - length)
            goto fail;
        end = base + length;

        if (region.type != QM_MEMORY_USABLE) {
            uint64_t lo;
            uint64_t hi;

            if (base >= MM_LIMIT)
                continue;
            if (firmware_reserved_count >= MM_MAX_RANGES) {
                error = MM_ERR_CAPACITY;
                goto fail;
            }

            lo = align_down(base);
            if (end >= MM_LIMIT)
                hi = MM_LIMIT;
            else if (!align_up_checked(end, &hi))
                hi = MM_LIMIT;

            if (hi > lo) {
                firmware_reserved_base[firmware_reserved_count] = lo;
                firmware_reserved_len[firmware_reserved_count] = hi - lo;
                ++firmware_reserved_count;
            }
            continue;
        }

        if (base < MM_LIMIT) {
            uint64_t lo;
            uint64_t hi;

            if (!align_up_checked(base, &lo))
                goto fail;
            if (end > MM_LIMIT)
                end = MM_LIMIT;
            hi = align_down(end);

            if (hi > lo) {
                if (ranges >= MM_MAX_RANGES) {
                    error = MM_ERR_CAPACITY;
                    goto fail;
                }
                usable[ranges] = lo;
                usable_len[ranges] = hi - lo;
                ++ranges;
            }
        }
    }

    if (ranges == 0) {
        error = MM_ERR_NO_MEMORY;
        goto fail;
    }

    /* Release every usable page once, even if firmware ranges overlap. */
    for (size_t r = 0; r < ranges; ++r) {
        uint64_t end = usable[r] + usable_len[r];
        for (uint64_t pa = usable[r]; pa < end; pa += MM_PAGE_SIZE) {
            size_t page = (size_t)(pa / MM_PAGE_SIZE);
            if (page < MM_MAX_PAGES && bit_get(page)) {
                bit_clear(page);
                ++free_count;
            }
        }
    }

    /* Non-usable firmware ranges override every overlapping usable range. */
    for (size_t f = 0; f < firmware_reserved_count; ++f) {
        uint64_t reserve_start = firmware_reserved_base[f];
        uint64_t reserve_end = reserve_start + firmware_reserved_len[f];

        for (size_t r = 0; r < ranges; ++r) {
            uint64_t range_end = usable[r] + usable_len[r];
            uint64_t lo = reserve_start > usable[r] ? reserve_start : usable[r];
            uint64_t hi = reserve_end < range_end ? reserve_end : range_end;

            for (uint64_t pa = lo; pa < hi; pa += MM_PAGE_SIZE) {
                size_t page = (size_t)(pa / MM_PAGE_SIZE);
                if (!bit_get(page)) {
                    bit_set(page);
                    if (free_count != 0)
                        --free_count;
                }
            }
        }
    }

    /* Apply explicit boot and caller reservations without double subtraction. */
    for (size_t r = 0; r < reserved_count; ++r) {
        uint64_t end = reserved_base[r] + reserved_len[r];
        uint64_t lo = align_down(reserved_base[r]);
        uint64_t hi;

        if (end >= MM_LIMIT)
            hi = MM_LIMIT;
        else if (!align_up_checked(end, &hi))
            hi = MM_LIMIT;
        if (lo > MM_LIMIT)
            lo = MM_LIMIT;

        for (uint64_t pa = lo; pa < hi; pa += MM_PAGE_SIZE) {
            size_t page = (size_t)(pa / MM_PAGE_SIZE);
            if (page < MM_MAX_PAGES && !bit_get(page)) {
                bit_set(page);
                if (free_count != 0)
                    --free_count;
            }
        }
    }

    if (free_count == 0) {
        error = MM_ERR_NO_MEMORY;
        goto fail;
    }

    total_pages = free_count;
    initialized = 1;
    return MM_OK;

fail:
    allocator_disable();
    firmware_reserved_count = 0;
    return error;
}

int mm_parse_multiboot_mmap(uint32_t addr, uint32_t length)
{
    qm_boot_memory_region_t parsed[MM_MAX_RANGES];
    size_t count = 0;
    uint32_t off = 0;

    allocator_disable();
    firmware_reserved_count = 0;

    /* The legacy parser dereferences this map through the first-GiB identity map. */
    if (addr == 0 || length == 0 || (uint64_t)addr + length > MM_LIMIT)
        return MM_ERR_ARGUMENT;

    while (off < length) {
        const uint8_t *p;
        uint32_t size;
        uint32_t type;
        uint64_t base;
        uint64_t region_length;

        if (length - off < 4U)
            goto malformed;
        p = (const uint8_t *)(uintptr_t)((uint64_t)addr + off);
        __builtin_memcpy(&size, p, sizeof(size));
        if (size < 20U || size > length - off - 4U)
            goto malformed;

        __builtin_memcpy(&base, p + 4U, sizeof(base));
        __builtin_memcpy(&region_length, p + 12U, sizeof(region_length));
        __builtin_memcpy(&type, p + 20U, sizeof(type));

        if (region_length != 0) {
            if (base > UINT64_MAX - region_length)
                goto malformed;
            if (count >= MM_MAX_RANGES) {
                allocator_disable();
                return MM_ERR_CAPACITY;
            }
            parsed[count].base = base;
            parsed[count].length = region_length;
            switch (type) {
            case 1: parsed[count].type = QM_MEMORY_USABLE; break;
            case 3: parsed[count].type = QM_MEMORY_ACPI_RECLAIM; break;
            case 4: parsed[count].type = QM_MEMORY_ACPI_NVS; break;
            case 5: parsed[count].type = QM_MEMORY_BAD; break;
            default: parsed[count].type = QM_MEMORY_RESERVED; break;
            }
            parsed[count].attributes = 1U;
            ++count;
        }
        off += size + 4U;
    }

    if (count == 0) {
        allocator_disable();
        return MM_ERR_NO_MEMORY;
    }
    return mm_parse_boot_regions(parsed, count, sizeof(parsed[0]));

malformed:
    allocator_disable();
    firmware_reserved_count = 0;
    return MM_ERR_FORMAT;
}

/* Multiboot v1 info layout: flags @0, mmap_length @44, mmap_addr @48. */
#define MB1_BOOT_MAGIC 0x2BADB002U
#define MB1_INFO_MMAP  (1U << 6)
#define MB1_INFO_SIZE  88U

int mm_init_from_multiboot(uint32_t magic, uint32_t info_addr,
                           uint64_t kernel_start, uint64_t kernel_end) {
    allocator_disable();
    /* Keep caller reservations but replace reservations from older boots. */
    remove_boot_reservations();

    if (magic != MB1_BOOT_MAGIC || !info_addr || kernel_end <= kernel_start)
        return MM_ERR_ARGUMENT;
    if ((uint64_t)info_addr + MB1_INFO_SIZE > MM_LIMIT)
        return MM_ERR_FORMAT;

    const uint8_t *info = (const uint8_t *)(uintptr_t)info_addr;
    uint32_t flags, mmap_length, mmap_addr;
    __builtin_memcpy(&flags, info, sizeof(flags));
    if (!(flags & MB1_INFO_MMAP)) return MM_ERR_FORMAT;
    __builtin_memcpy(&mmap_length, info + 44, sizeof(mmap_length));
    __builtin_memcpy(&mmap_addr, info + 48, sizeof(mmap_addr));
    if (!mmap_addr || !mmap_length || (uint64_t)mmap_addr + mmap_length > MM_LIMIT)
        return MM_ERR_FORMAT;

    /* Preserve caller reservations across init attempts; boot reservations are
     * tagged and removed on the next init or if this one fails. */
    /* Keep legacy low memory (IVT, BIOS data, EBDA, DMA areas) unavailable. */
    int rc = mm_reserve_internal(0, 0x100000ULL, 1);
    if (rc == MM_OK) rc = mm_reserve_internal(0x100000ULL, 0x100000ULL, 1);
    if (rc == MM_OK) rc = mm_reserve_internal(kernel_start, kernel_end - kernel_start, 1);
    if (rc == MM_OK) rc = mm_reserve_internal(info_addr, MB1_INFO_SIZE, 1);
    if (rc == MM_OK) rc = mm_reserve_internal(mmap_addr, mmap_length, 1);
    if (rc == MM_OK) rc = mm_parse_multiboot_mmap(mmap_addr, mmap_length);
    if (rc != MM_OK) {
        allocator_disable();
        remove_boot_reservations();
    }
    return rc;
}

int mm_init_from_boot_info(const qm_boot_info_t *info,
                           uint64_t bootloader_info_base,
                           uint64_t bootloader_info_size,
                           uint64_t bootloader_map_base,
                           uint64_t bootloader_map_size)
{
    uintptr_t info_address;
    uint64_t map_bytes;
    uint64_t module_bytes = 0;
    const qm_boot_memory_region_t *regions;
    const qm_boot_module_t *modules = NULL;
    int rc;

    allocator_disable();
    remove_boot_reservations();
    firmware_reserved_count = 0;

    if (info == NULL)
        return MM_ERR_ARGUMENT;
    if (qm_boot_validate_info(info) != QM_OK ||
        info->architecture != QM_BOOT_ARCH_X86_64)
        return MM_ERR_FORMAT;

    info_address = (uintptr_t)info;
    if ((uint64_t)info_address >= MM_LIMIT ||
        (uint64_t)info->struct_size > MM_LIMIT - (uint64_t)info_address)
        return MM_ERR_ARGUMENT;

    if (info->memory_map_count > MM_MAX_RANGES ||
        info->memory_map_entry_size < sizeof(qm_boot_memory_region_t))
        return MM_ERR_FORMAT;
    map_bytes = (uint64_t)info->memory_map_count *
                (uint64_t)info->memory_map_entry_size;
    if (info->memory_map_physical >= MM_LIMIT ||
        map_bytes > MM_LIMIT - info->memory_map_physical ||
        (info->memory_map_physical & (sizeof(uint64_t) - 1U)) != 0)
        return MM_ERR_FORMAT;

    regions = (const qm_boot_memory_region_t *)(uintptr_t)
              info->memory_map_physical;

    if ((bootloader_info_size != 0 &&
         bootloader_info_base > UINT64_MAX - bootloader_info_size) ||
        (bootloader_map_size != 0 &&
         bootloader_map_base > UINT64_MAX - bootloader_map_size))
        return MM_ERR_ARGUMENT;

    if (info->module_count != 0) {
        module_bytes = (uint64_t)info->module_count * sizeof(qm_boot_module_t);
        if (info->modules_physical >= MM_LIMIT ||
            module_bytes > MM_LIMIT - info->modules_physical ||
            (info->modules_physical & (sizeof(uint64_t) - 1U)) != 0)
            return MM_ERR_FORMAT;
        modules = (const qm_boot_module_t *)(uintptr_t)
                  info->modules_physical;
    }

#define MM_RESERVE_BOOT(base_, size_) do { \
        rc = mm_reserve_internal((uint64_t)(base_), (uint64_t)(size_), 1); \
        if (rc != MM_OK) goto reserve_failed; \
    } while (0)

    /* Keep low firmware memory and the historical 1-2 MiB bootstrap window. */
    MM_RESERVE_BOOT(0, 0x100000ULL);
    MM_RESERVE_BOOT(0x100000ULL, 0x100000ULL);
    MM_RESERVE_BOOT(info->kernel_physical_base,
                    info->kernel_physical_size);
    MM_RESERVE_BOOT(info_address, info->struct_size);
    MM_RESERVE_BOOT(info->memory_map_physical, map_bytes);

    /* The old loader-owned records are no longer parsed by PMM, but remain
     * reserved so the allocator cannot reuse memory still owned by firmware. */
    if (bootloader_info_size != 0)
        MM_RESERVE_BOOT(bootloader_info_base, bootloader_info_size);
    if (bootloader_map_size != 0)
        MM_RESERVE_BOOT(bootloader_map_base, bootloader_map_size);

    if (info->framebuffer.size != 0)
        MM_RESERVE_BOOT(info->framebuffer.physical_address,
                        info->framebuffer.size);

    if (info->module_count != 0) {
        MM_RESERVE_BOOT(info->modules_physical, module_bytes);
        for (qm_u32_t i = 0; i < info->module_count; ++i) {
            if (modules[i].size == 0 ||
                modules[i].physical_start > UINT64_MAX - modules[i].size) {
                rc = MM_ERR_FORMAT;
                goto reserve_failed;
            }
            MM_RESERVE_BOOT(modules[i].physical_start, modules[i].size);
        }
    }

    rc = mm_parse_boot_regions(regions, info->memory_map_count,
                               info->memory_map_entry_size);
    if (rc != MM_OK)
        goto reserve_failed;

#undef MM_RESERVE_BOOT
    return MM_OK;

reserve_failed:
    allocator_disable();
    firmware_reserved_count = 0;
    remove_boot_reservations();
#undef MM_RESERVE_BOOT
    return rc;
}

int mm_alloc_page(uint64_t *out) {
    if (!initialized || !out) return MM_ERR_ARGUMENT;
    if (!free_count) return MM_ERR_NO_MEMORY;
    for (size_t r = 0; r < ranges; ++r) {
        uint64_t end = usable[r] + usable_len[r];

            /* RING3_DIRECT_MAP_LIMIT */
            const uint64_t direct_map_limit = 1ULL << 30;
            if (usable[r] >= direct_map_limit)
                continue;
            if (end > direct_map_limit)
                end = direct_map_limit;
        for (uint64_t pa = usable[r]; pa < end; pa += MM_PAGE_SIZE) {
            size_t p = (size_t)(pa / MM_PAGE_SIZE);
            if (p < MM_MAX_PAGES && !bit_get(p)) {
                bit_set(p);
                --free_count;
                *out = pa;
                return MM_OK;
            }
        }
    }
    return MM_ERR_NO_MEMORY;
}

int mm_free_page(uint64_t pa) {
    if (!initialized || (pa & (MM_PAGE_SIZE - 1)) || pa >= MM_LIMIT)
        return MM_ERR_ARGUMENT;
    size_t p = (size_t)(pa / MM_PAGE_SIZE);
    int belongs = 0;
    for (size_t r = 0; r < ranges; ++r) {
        if (pa >= usable[r] && pa < usable[r] + usable_len[r]) {
            belongs = 1;
            break;
        }
    }
    if (!belongs || !bit_get(p) || range_is_reserved(pa)) return MM_ERR_ARGUMENT;
    bit_clear(p);
    ++free_count;
    return MM_OK;
}

int mm_self_test(void) {
    if (!initialized) return MM_ERR_ARGUMENT;
    size_t before = free_count;
    uint64_t page = 0;
    if (!before || mm_alloc_page(&page) != MM_OK) return MM_ERR_NO_MEMORY;
    if ((page & (MM_PAGE_SIZE - 1)) || page >= MM_LIMIT || free_count + 1 != before) {
        (void)mm_free_page(page);
        return MM_ERR_FORMAT;
    }
    if (mm_free_page(page) != MM_OK || free_count != before)
        return MM_ERR_FORMAT;
    /* Double-free must be rejected without changing the free count. */
    if (mm_free_page(page) == MM_OK || free_count != before)
        return MM_ERR_FORMAT;
    return MM_OK;
}

/* Total managed pages excludes reserved pages but includes allocated pages;
 * free_pages reports the current allocatable count. */
size_t mm_available_pages(void) { return total_pages; }
size_t mm_free_pages(void) { return free_count; }
size_t mm_range_count(void) { return ranges; }
