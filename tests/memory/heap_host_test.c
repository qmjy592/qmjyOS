#include "qmjy/heap.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned int failures;

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #expression); \
        ++failures; \
    } else { \
        printf("[PASS] %s\n", #expression); \
    } \
} while (0)

static void test_basic_and_alignment(void)
{
    void *a = kmalloc(1);
    void *b = kmalloc(17);
    qm_heap_stats_t stats;

    CHECK(a != NULL);
    CHECK(b != NULL);
    CHECK(((uintptr_t)a & (QM_HEAP_ALIGNMENT - 1U)) == 0);
    CHECK(((uintptr_t)b & (QM_HEAP_ALIGNMENT - 1U)) == 0);
    CHECK(qm_heap_get_stats(&stats) == QM_OK);
    CHECK(stats.allocated_blocks == 2);
    CHECK(stats.free_blocks >= 1);
    CHECK(kfree(a) == QM_OK);
    CHECK(kfree(b) == QM_OK);
}

static void test_calloc_and_invalid_free(void)
{
    unsigned char *memory = (unsigned char *)kcalloc(32, 3);
    int foreign = 7;

    CHECK(memory != NULL);
    if (memory) {
        int all_zero = 1;
        for (unsigned int i = 0; i < 96; ++i) {
            if (memory[i] != 0) all_zero = 0;
        }
        CHECK(all_zero);
        CHECK(kfree(memory) == QM_OK);
        CHECK(kfree(memory) == QM_ERR_STATE);
        CHECK(kfree(memory + QM_HEAP_ALIGNMENT) == QM_ERR_INVALID);
    }

    CHECK(kcalloc((qm_size_t)-1, 2) == NULL);
    CHECK(kmalloc(0) == NULL);
    CHECK(kfree(&foreign) == QM_ERR_INVALID);
    CHECK(kfree(NULL) == QM_OK);
}

static void test_coalescing(void)
{
    void *a = kmalloc(1024);
    void *b = kmalloc(1024);
    void *c = kmalloc(1024);
    qm_heap_stats_t before;
    qm_heap_stats_t after;

    CHECK(a != NULL && b != NULL && c != NULL);
    CHECK(qm_heap_get_stats(&before) == QM_OK);
    CHECK(kfree(a) == QM_OK);
    CHECK(kfree(c) == QM_OK);
    CHECK(kfree(b) == QM_OK);
    CHECK(qm_heap_get_stats(&after) == QM_OK);
    CHECK(after.free_blocks == 1);
    CHECK(after.free_bytes > before.free_bytes);
    CHECK(after.largest_free_block == after.free_bytes);
}

static void test_exhaustion_and_recovery(void)
{
    void *blocks[2048];
    qm_size_t count = 0;
    qm_heap_stats_t stats;

    while (count < (qm_size_t)(sizeof(blocks) / sizeof(blocks[0]))) {
        void *p = kmalloc(128);
        if (!p) break;
        blocks[count++] = p;
    }

    CHECK(count > 0);
    CHECK(kmalloc(128) == NULL);
    {
        int freed_all = 1;
        for (qm_size_t i = 0; i < count; ++i) {
            if (kfree(blocks[i]) != QM_OK) freed_all = 0;
        }
        CHECK(freed_all);
    }

    CHECK(qm_heap_get_stats(&stats) == QM_OK);
    CHECK(stats.free_blocks == 1);
    CHECK(stats.allocated_blocks == 0);
}

int main(void)
{
    CHECK(qm_heap_init() == QM_OK);
    CHECK(qm_heap_init() == QM_ERR_BUSY);
    test_basic_and_alignment();
    test_calloc_and_invalid_free();
    test_coalescing();
    test_exhaustion_and_recovery();

    if (failures) {
        fprintf(stderr, "Heap tests failed: %u\n", failures);
        return EXIT_FAILURE;
    }
    puts("Kernel heap host tests: PASS");
    return EXIT_SUCCESS;
}
