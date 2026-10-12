#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "src/memory.h"

#define TEST_INFO_ADDR 0x10000000U
#define TEST_MAP_ADDR  0x10010000U
#define TEST_MAGIC     0x2BADB002U
#define TEST_MMAP_FLAG (1U << 6)

static void must(int cond, const char *msg) {
    if (!cond) { fprintf(stderr, "FAIL: %s\n", msg); exit(1); }
    printf("PASS: %s\n", msg);
}

int main(void) {
    void *info = mmap((void *)(uintptr_t)TEST_INFO_ADDR, 4096,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    void *map = mmap((void *)(uintptr_t)TEST_MAP_ADDR, 4096,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (info == MAP_FAILED || map == MAP_FAILED) {
        fprintf(stderr, "SKIP: low-address mmap unavailable (info=%p map=%p errno=%d)\n", info, map, errno);
        return 77;
    }

    memset(info, 0, 4096);
    memset(map, 0, 4096);
    uint8_t *i = info, *m = map;
    uint32_t flags = TEST_MMAP_FLAG, mmap_length = 48, mmap_addr = TEST_MAP_ADDR;
    memcpy(i, &flags, sizeof(flags));
    memcpy(i + 44, &mmap_length, sizeof(mmap_length));
    memcpy(i + 48, &mmap_addr, sizeof(mmap_addr));

    /* Two usable type-1 entries overlap from 32 MiB to 64 MiB. */
    uint32_t entry_size = 20, type = 1;
    uint64_t base1 = 0, len1 = 64ULL * 1024 * 1024;
    uint64_t base2 = 32ULL * 1024 * 1024, len2 = 32ULL * 1024 * 1024;
    memcpy(m, &entry_size, 4); memcpy(m + 4, &base1, 8); memcpy(m + 12, &len1, 8); memcpy(m + 20, &type, 4);
    memcpy(m + 24, &entry_size, 4); memcpy(m + 28, &base2, 8); memcpy(m + 36, &len2, 8); memcpy(m + 44, &type, 4);

    must(mm_reserve(9ULL * 1024 * 1024, 4096) == MM_OK, "caller reservation accepted before init");
    must(mm_init_from_multiboot(TEST_MAGIC, TEST_INFO_ADDR, 2ULL * 1024 * 1024, 3ULL * 1024 * 1024) == MM_OK,
         "initialization accepts a valid Multiboot map");
    must(mm_available_pages() == mm_free_pages(), "initial free and managed counts agree");
    must(mm_range_count() == 2, "overlapping usable ranges are preserved without double-counting pages");
    must(mm_free_page(0) == MM_ERR_ARGUMENT, "low-memory/zero page cannot be freed");
    must(mm_free_page(9ULL * 1024 * 1024) == MM_ERR_ARGUMENT, "caller-reserved page cannot be freed");
    must(mm_self_test() == MM_OK, "allocator self-test covers alloc/free/double-free");

    uint64_t page = 0;
    must(mm_alloc_page(&page) == MM_OK, "page allocation succeeds");
    must(page >= 3ULL * 1024 * 1024, "allocator skips the first 2 MiB and kernel image");
    size_t free_before_reserve = mm_free_pages();
    size_t managed_before_reserve = mm_available_pages();
    must(mm_reserve(10ULL * 1024 * 1024, 4096) == MM_OK, "runtime reservation accepted for a free page");
    must(mm_free_pages() + 1 == free_before_reserve, "runtime reservation decrements free count once");
    must(mm_available_pages() + 1 == managed_before_reserve, "runtime reservation decrements managed count once");
    must(mm_free_page(10ULL * 1024 * 1024) == MM_ERR_ARGUMENT, "runtime-reserved page cannot be freed");
    must(mm_reserve(page, 4096) == MM_ERR_ARGUMENT, "reservation cannot silently invalidate a live allocation");
    must(mm_free_page(page) == MM_OK, "allocated page can be freed");
    must(mm_free_page(page) == MM_ERR_ARGUMENT, "double-free is rejected");

    /* Reinitialize with a different kernel range. The former boot range must
     * become usable, while caller-owned reservations survive. */
    must(mm_init_from_multiboot(TEST_MAGIC, TEST_INFO_ADDR, 3ULL * 1024 * 1024, 4ULL * 1024 * 1024) == MM_OK,
         "second initialization succeeds with changed kernel range");
    must(mm_free_page(9ULL * 1024 * 1024) == MM_ERR_ARGUMENT, "caller reservation survives reinitialization");
    must(mm_self_test() == MM_OK, "self-test passes after reinitialization");
    must(mm_alloc_page(&page) == MM_OK && page == 2ULL * 1024 * 1024,
         "former kernel range becomes usable after boot reservation rebuild");
    must(mm_free_page(page) == MM_OK, "page from former kernel range can be freed");
    for (uint64_t i = 0; i < 40; ++i) {
        uint64_t start = (4ULL + i) * 1024 * 1024;
        must(mm_init_from_multiboot(TEST_MAGIC, TEST_INFO_ADDR, start, start + 1024 * 1024) == MM_OK,
             "repeated initialization does not leak boot reservation slots");
    }

    /* Invalid parse must disable the allocator rather than leave stale state active. */
    must(mm_parse_multiboot_mmap(0, 48) == MM_ERR_ARGUMENT, "invalid map address rejected");
    must(mm_range_count() == 0 && mm_free_pages() == 0, "failed parse clears active allocator state");
    must(mm_alloc_page(&page) == MM_ERR_ARGUMENT, "allocation rejected after failed parse");

    /* Restore a malformed size field, try init, and verify rollback. */
    uint32_t bad_size = 5000;
    memcpy(m, &bad_size, 4);
    must(mm_init_from_multiboot(TEST_MAGIC, TEST_INFO_ADDR, 2ULL * 1024 * 1024, 3ULL * 1024 * 1024) == MM_ERR_FORMAT,
         "malformed entry length rejected");
    must(mm_range_count() == 0 && mm_free_pages() == 0, "failed initialization rolls back active state");

    /* Restore a valid-sized type-1 record but use overflowing base+length. */
    entry_size = 20;
    uint64_t overflow_base = UINT64_MAX - 0x1000ULL, overflow_len = 0x2000ULL;
    memcpy(m, &entry_size, 4);
    memcpy(m + 4, &overflow_base, 8);
    memcpy(m + 12, &overflow_len, 8);
    memcpy(m + 20, &type, 4);
    must(mm_init_from_multiboot(TEST_MAGIC, TEST_INFO_ADDR, 2ULL * 1024 * 1024, 3ULL * 1024 * 1024) == MM_ERR_FORMAT,
         "overflowing type-1 memory range rejected");
    must(mm_range_count() == 0 && mm_free_pages() == 0, "overflowing range leaves allocator disabled");

    munmap(info, 4096); munmap(map, 4096);
    puts("ALL TESTS PASSED");
    return 0;
}
