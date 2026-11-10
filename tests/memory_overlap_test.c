#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "memory.h"

#define INFO_ADDR 0x10000000U
#define MAP_ADDR  0x10010000U

int main(void) {
    uint8_t *info = mmap((void *)(uintptr_t)INFO_ADDR, 4096,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    uint8_t *map = mmap((void *)(uintptr_t)MAP_ADDR, 4096,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

    if (info == MAP_FAILED || map == MAP_FAILED) {
        puts("SKIP: low-address mmap unavailable");
        return 77;
    }

    memset(info, 0, 4096);
    memset(map, 0, 4096);

    uint32_t flags = 1U << 6;
    uint32_t length = 48;
    uint32_t address = MAP_ADDR;
    memcpy(info, &flags, 4);
    memcpy(info + 44, &length, 4);
    memcpy(info + 48, &address, 4);

    uint32_t size = 20;
    uint32_t usable_type = 1;
    uint32_t reserved_type = 2;
    uint64_t usable_base = 2ULL * 1024 * 1024;
    uint64_t usable_length = 32ULL * 1024 * 1024;
    uint64_t reserved_base = 3ULL * 1024 * 1024;
    uint64_t reserved_length = 1ULL * 1024 * 1024;

    memcpy(map, &size, 4);
    memcpy(map + 4, &usable_base, 8);
    memcpy(map + 12, &usable_length, 8);
    memcpy(map + 20, &usable_type, 4);

    memcpy(map + 24, &size, 4);
    memcpy(map + 28, &reserved_base, 8);
    memcpy(map + 36, &reserved_length, 8);
    memcpy(map + 44, &reserved_type, 4);

    if (mm_init_from_multiboot(0x2BADB002U, INFO_ADDR,
            2ULL * 1024 * 1024, 3ULL * 1024 * 1024) != MM_OK) {
        puts("FAIL: allocator initialization");
        return 1;
    }

    if (mm_free_page(3ULL * 1024 * 1024) != MM_ERR_ARGUMENT) {
        puts("FAIL: non-usable reserved page was freed");
        return 1;
    }
    puts("PASS: non-usable reserved page cannot be freed");

    uint64_t page = 0;
    if (mm_alloc_page(&page) != MM_OK) {
        puts("FAIL: allocation");
        return 1;
    }

    if (page != 4ULL * 1024 * 1024) {
        printf("FAIL: reserved overlap allocated: 0x%llx\n",
               (unsigned long long)page);
        return 1;
    }

    puts("PASS: non-usable overlap takes precedence");
    mm_free_page(page);
    munmap(info, 4096);
    munmap(map, 4096);
    puts("ALL OVERLAP TESTS PASSED");
    return 0;
}
