#include "qmjy/heap.h"

#include <stdint.h>
#include <stddef.h>

#define QM_HEAP_MAGIC 0x514D4A5948454150ULL
#define QM_HEAP_FREE 0U
#define QM_HEAP_USED 1U
#define QM_HEAP_MAX_BLOCKS ((QM_HEAP_SIZE / (sizeof(struct qm_heap_block) + QM_HEAP_ALIGNMENT)) + 2U)

struct qm_heap_block {
    qm_u64_t magic;
    qm_size_t size;
    struct qm_heap_block *previous;
    struct qm_heap_block *next;
    qm_u32_t state;
    qm_u32_t reserved;
} __attribute__((aligned(QM_HEAP_ALIGNMENT)));

static qm_u8_t g_heap_arena[QM_HEAP_SIZE]
    __attribute__((aligned(QM_HEAP_ALIGNMENT)));
static struct qm_heap_block *g_heap_first;
static qm_bool_t g_heap_initialized;

#define QM_HEAP_EFFECTIVE_SIZE \
    ((qm_size_t)(QM_HEAP_SIZE) & ~((qm_size_t)QM_HEAP_ALIGNMENT - 1U))

static int qm_heap_add_overflow(qm_size_t a, qm_size_t b, qm_size_t *out)
{
    if (!out || a > (qm_size_t)-1 - b)
        return 1;
    *out = a + b;
    return 0;
}

static int qm_heap_align_size(qm_size_t value, qm_size_t *out)
{
    const qm_size_t mask = (qm_size_t)QM_HEAP_ALIGNMENT - 1U;

    if (!out || value == 0 || value > (qm_size_t)-1 - mask)
        return 0;

    *out = (value + mask) & ~mask;
    return *out != 0;
}

static qm_bool_t qm_heap_block_valid(const struct qm_heap_block *block)
{
    const uintptr_t arena_start = (uintptr_t)g_heap_arena;
    const uintptr_t arena_end = arena_start + QM_HEAP_EFFECTIVE_SIZE;
    const uintptr_t header = (uintptr_t)block;
    uintptr_t payload;

    if (!block || header < arena_start || header > arena_end)
        return QM_FALSE;
    if ((header & (QM_HEAP_ALIGNMENT - 1U)) != 0)
        return QM_FALSE;
    if (header > arena_end - sizeof(*block))
        return QM_FALSE;
    if (block->magic != QM_HEAP_MAGIC)
        return QM_FALSE;
    if (block->state != QM_HEAP_FREE && block->state != QM_HEAP_USED)
        return QM_FALSE;

    payload = header + sizeof(*block);
    if (block->size == 0 || block->size > arena_end - payload)
        return QM_FALSE;

    if (block->next) {
        uintptr_t next = (uintptr_t)block->next;
        if (next < payload || next > arena_end - sizeof(*block))
            return QM_FALSE;
        if ((next & (QM_HEAP_ALIGNMENT - 1U)) != 0)
            return QM_FALSE;
        if (block->next->previous != block)
            return QM_FALSE;
    }
    if (block->previous) {
        uintptr_t previous = (uintptr_t)block->previous;
        if (previous < arena_start || previous > arena_end - sizeof(*block))
            return QM_FALSE;
        if ((previous & (QM_HEAP_ALIGNMENT - 1U)) != 0)
            return QM_FALSE;
        if (block->previous->next != block)
            return QM_FALSE;
    }

    return QM_TRUE;
}

static qm_bool_t qm_heap_list_contains(const struct qm_heap_block *target)
{
    struct qm_heap_block *block = g_heap_first;
    qm_size_t visited = 0;

    while (block && visited++ < QM_HEAP_MAX_BLOCKS) {
        if (!qm_heap_block_valid(block))
            return QM_FALSE;
        if (block == target)
            return QM_TRUE;
        block = block->next;
    }
    return QM_FALSE;
}

qm_status_t qm_heap_init(void)
{
    struct qm_heap_block *first;

    if (g_heap_initialized)
        return QM_ERR_BUSY;
    if (QM_HEAP_EFFECTIVE_SIZE <= sizeof(*first) + QM_HEAP_ALIGNMENT)
        return QM_ERR_NO_SPACE;

    first = (struct qm_heap_block *)(void *)g_heap_arena;
    first->magic = QM_HEAP_MAGIC;
    first->size = QM_HEAP_EFFECTIVE_SIZE - sizeof(*first);
    first->previous = NULL;
    first->next = NULL;
    first->state = QM_HEAP_FREE;
    first->reserved = 0;

    g_heap_first = first;
    g_heap_initialized = QM_TRUE;
    return QM_OK;
}

static qm_status_t qm_heap_split(struct qm_heap_block *block, qm_size_t wanted)
{
    qm_size_t minimum;
    qm_size_t remainder;
    struct qm_heap_block *tail;

    if (qm_heap_add_overflow(wanted, sizeof(*block), &minimum) ||
        qm_heap_add_overflow(minimum, QM_HEAP_ALIGNMENT, &minimum))
        return QM_ERR_OVERFLOW;

    if (block->size < minimum)
        return QM_OK;

    remainder = block->size - wanted - sizeof(*block);
    tail = (struct qm_heap_block *)((qm_u8_t *)(block + 1) + wanted);

    tail->magic = QM_HEAP_MAGIC;
    tail->size = remainder;
    tail->previous = block;
    tail->next = block->next;
    tail->state = QM_HEAP_FREE;
    tail->reserved = 0;

    if (tail->next)
        tail->next->previous = tail;

    block->next = tail;
    block->size = wanted;
    return QM_OK;
}

void *kmalloc(qm_size_t size)
{
    struct qm_heap_block *block;
    qm_size_t wanted;
    qm_size_t visited = 0;

    if (!g_heap_initialized || !qm_heap_align_size(size, &wanted))
        return NULL;

    block = g_heap_first;
    while (block && visited++ < QM_HEAP_MAX_BLOCKS) {
        if (!qm_heap_block_valid(block))
            return NULL;

        if (block->state == QM_HEAP_FREE && block->size >= wanted) {
            if (qm_heap_split(block, wanted) != QM_OK)
                return NULL;
            block->state = QM_HEAP_USED;
            return (void *)(block + 1);
        }
        block = block->next;
    }

    return NULL;
}

void *kcalloc(qm_size_t count, qm_size_t size)
{
    qm_size_t total;
    qm_u8_t *memory;

    if (count == 0 || size == 0 || count > (qm_size_t)-1 / size)
        return NULL;

    total = count * size;
    memory = (qm_u8_t *)kmalloc(total);
    if (!memory)
        return NULL;

    for (qm_size_t i = 0; i < total; ++i)
        memory[i] = 0;
    return memory;
}

static void qm_heap_merge_next(struct qm_heap_block *block)
{
    struct qm_heap_block *next = block->next;
    uintptr_t expected;

    if (!next || !qm_heap_block_valid(next) ||
        next->state != QM_HEAP_FREE)
        return;

    expected = (uintptr_t)(block + 1) + block->size;
    if (expected != (uintptr_t)next)
        return;

    if (block->size > (qm_size_t)-1 - sizeof(*next) - next->size)
        return;

    block->size += sizeof(*next) + next->size;
    block->next = next->next;
    if (block->next)
        block->next->previous = block;

    next->magic = 0;
    next->size = 0;
    next->previous = NULL;
    next->next = NULL;
    next->state = QM_HEAP_FREE;
    next->reserved = 0;
}

qm_status_t kfree(void *pointer)
{
    struct qm_heap_block *block;
    uintptr_t address;
    uintptr_t arena_start = (uintptr_t)g_heap_arena;
    uintptr_t arena_end = arena_start + QM_HEAP_EFFECTIVE_SIZE;

    if (!pointer)
        return QM_OK;
    if (!g_heap_initialized)
        return QM_ERR_STATE;

    address = (uintptr_t)pointer;
    if (address < arena_start + sizeof(*block) || address >= arena_end ||
        (address & (QM_HEAP_ALIGNMENT - 1U)) != 0)
        return QM_ERR_INVALID;

    block = (struct qm_heap_block *)(void *)(address - sizeof(*block));
    if (!qm_heap_list_contains(block))
        return QM_ERR_INVALID;
    if (!qm_heap_block_valid(block))
        return QM_ERR_CORRUPT;
    if (block->state != QM_HEAP_USED)
        return QM_ERR_STATE;

    block->state = QM_HEAP_FREE;

    if (block->previous && block->previous->state == QM_HEAP_FREE) {
        struct qm_heap_block *previous = block->previous;
        uintptr_t expected = (uintptr_t)(previous + 1) + previous->size;
        if (expected == (uintptr_t)block) {
            qm_heap_merge_next(previous);
            block = previous;
        }
    }

    qm_heap_merge_next(block);
    return QM_OK;
}

qm_status_t qm_heap_get_stats(qm_heap_stats_t *stats)
{
    struct qm_heap_block *block;
    qm_size_t visited = 0;
    qm_heap_stats_t result;

    if (!stats)
        return QM_ERR_INVALID;
    if (!g_heap_initialized)
        return QM_ERR_STATE;

    result.arena_bytes = QM_HEAP_EFFECTIVE_SIZE;
    result.free_bytes = 0;
    result.largest_free_block = 0;
    result.allocated_blocks = 0;
    result.free_blocks = 0;

    block = g_heap_first;
    while (block) {
        if (visited++ >= QM_HEAP_MAX_BLOCKS || !qm_heap_block_valid(block))
            return QM_ERR_CORRUPT;

        if (block->state == QM_HEAP_FREE) {
            result.free_bytes += block->size;
            ++result.free_blocks;
            if (block->size > result.largest_free_block)
                result.largest_free_block = block->size;
        } else {
            ++result.allocated_blocks;
        }
        block = block->next;
    }

    *stats = result;
    return QM_OK;
}
