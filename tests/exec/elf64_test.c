/* QMJYOS_ELF64_HARDENING_V1 */
#include "qmjy/elf64.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define TEST_IMAGE_SIZE 1024U
#define TEST_MEMORY_BASE 0x40000000ULL
#define TEST_MEMORY_SIZE 0x10000U
#define TEST_PT_LOAD 1U
#define TEST_PT_DYNAMIC 2U
#define TEST_PT_INTERP 3U
#define TEST_PT_GNU_STACK 0x6474E551U
#define TEST_PT_GNU_RELRO 0x6474E552U
#define TEST_PF_R 4U
#define TEST_PF_W 2U
#define TEST_PF_X 1U

struct test_ehdr {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed));

struct test_phdr {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} __attribute__((packed));

static uint8_t memory[TEST_MEMORY_SIZE];
static uint32_t maps;
static uint32_t unmaps;
static int fail_second_map;
static int fail_unmap;
static int null_first_view;

static qm_status_t test_map(void *context, qm_u64_t base, qm_u64_t size,
                            qm_u32_t protection, void **view)
{
    (void)context;
    (void)protection;
    ++maps;
    *view = NULL;
    if (fail_second_map && maps == 2U)
        return QM_ERR_NO_MEMORY;
    if (null_first_view && maps == 1U)
        return QM_OK;
    if (base < TEST_MEMORY_BASE || size > TEST_MEMORY_SIZE ||
        base - TEST_MEMORY_BASE > TEST_MEMORY_SIZE - size)
        return QM_ERR_RANGE;
    *view = &memory[base - TEST_MEMORY_BASE];
    return QM_OK;
}

static qm_status_t test_unmap(void *context, qm_u64_t base, qm_u64_t size)
{
    (void)context;
    (void)base;
    (void)size;
    ++unmaps;
    return fail_unmap ? QM_ERR_IO : QM_OK;
}

static struct test_phdr *program_headers(uint8_t *bytes)
{
    struct test_ehdr *eh = (struct test_ehdr *)bytes;
    return (struct test_phdr *)(bytes + eh->phoff);
}

static void make_image(uint8_t *bytes, size_t length)
{
    struct test_ehdr *eh;
    struct test_phdr *ph;
    size_t i;

    assert(length >= TEST_IMAGE_SIZE);
    for (i = 0; i < length; ++i)
        bytes[i] = 0;

    eh = (struct test_ehdr *)bytes;
    eh->ident[0] = 0x7f;
    eh->ident[1] = 'E';
    eh->ident[2] = 'L';
    eh->ident[3] = 'F';
    eh->ident[4] = 2;
    eh->ident[5] = 1;
    eh->ident[6] = 1;
    eh->ident[7] = 0;
    eh->ident[8] = 0;
    eh->type = 2;
    eh->machine = 62;
    eh->version = 1;
    eh->entry = 0x40000180;
    eh->phoff = sizeof(*eh);
    eh->ehsize = sizeof(*eh);
    eh->phentsize = sizeof(*ph);
    eh->phnum = 2;

    ph = (struct test_phdr *)(bytes + eh->phoff);
    ph[0].type = TEST_PT_LOAD;
    ph[0].flags = TEST_PF_R | TEST_PF_X;
    ph[0].offset = 0x180;
    ph[0].vaddr = 0x40000180;
    ph[0].filesz = 4;
    ph[0].memsz = 4;
    ph[0].align = 0x1000;

    ph[1].type = TEST_PT_LOAD;
    ph[1].flags = TEST_PF_R | TEST_PF_W;
    ph[1].offset = 0x200;
    ph[1].vaddr = 0x40001200;
    ph[1].filesz = 4;
    ph[1].memsz = 32;
    ph[1].align = 0x1000;

    bytes[0x180] = 0x90;
    bytes[0x181] = 0x90;
    bytes[0x182] = 0xC3;
    bytes[0x183] = 0x90;
    bytes[0x200] = 'D';
    bytes[0x201] = 'A';
    bytes[0x202] = 'T';
    bytes[0x203] = 'A';
}

static void assert_image_zero(const qm_elf64_image_t *image)
{
    const uint8_t *bytes = (const uint8_t *)image;
    size_t i;

    for (i = 0; i < sizeof(*image); ++i)
        assert(bytes[i] == 0);
}

static void expect_load_failure(uint8_t *bytes, qm_status_t expected,
                                int expect_no_callbacks)
{
    qm_elf64_image_t image;
    qm_elf64_memory_ops_t ops = { test_map, test_unmap };
    uint8_t *output = (uint8_t *)&image;
    size_t i;

    for (i = 0; i < sizeof(image); ++i)
        output[i] = 0xA5;
    maps = 0;
    unmaps = 0;
    assert(qm_elf64_load(bytes, TEST_IMAGE_SIZE, &ops, NULL, &image) == expected);
    assert_image_zero(&image);
    if (expect_no_callbacks)
        assert(maps == 0U && unmaps == 0U);
}

int main(void)
{
    uint8_t bytes[TEST_IMAGE_SIZE];
    uint8_t unaligned[TEST_IMAGE_SIZE + 1U];
    qm_elf64_image_t image;
    qm_elf64_image_t malformed;
    qm_elf64_memory_ops_t ops = { test_map, test_unmap };
    struct test_ehdr *eh;
    struct test_phdr *ph;

    /* Unaligned input buffers and the normal load/BSS/unload path. */
    make_image(unaligned + 1U, TEST_IMAGE_SIZE);
    maps = unmaps = 0;
    assert(qm_elf64_load(unaligned + 1U, TEST_IMAGE_SIZE, &ops, NULL,
                         &image) == QM_OK);
    assert(qm_elf64_unload(&ops, NULL, &image) == QM_OK);

    make_image(bytes, sizeof(bytes));
    maps = unmaps = 0;
    assert(qm_elf64_load(bytes, sizeof(bytes), &ops, NULL, &image) == QM_OK);
    assert(image.entry == 0x40000180 && image.segment_count == 2U);
    assert(memory[0x180] == 0x90 && memory[0x181] == 0x90 &&
           memory[0x182] == 0xC3);
    assert(memory[0x1200] == 'D' && memory[0x1201] == 'A');
    for (unsigned int i = 4; i < 32U; ++i)
        assert(memory[0x1200 + i] == 0U);

    /* Malformed unload descriptors must be rejected before any unmap. */
    malformed = image;
    malformed.segments[1].map_size += QM_ELF64_PAGE_SIZE;
    unmaps = 0;
    assert(qm_elf64_unload(&ops, NULL, &malformed) == QM_ERR_CORRUPT);
    assert(unmaps == 0U);

    malformed = image;
    malformed.entry = 0x40001200; /* Entry is now inside the non-executable data segment. */
    unmaps = 0;
    assert(qm_elf64_unload(&ops, NULL, &malformed) == QM_ERR_CORRUPT);
    assert(unmaps == 0U);

    malformed = image;
    malformed.segments[QM_ELF64_MAX_SEGMENTS - 1U].map_address = TEST_MEMORY_BASE;
    unmaps = 0;
    assert(qm_elf64_unload(&ops, NULL, &malformed) == QM_ERR_CORRUPT);
    assert(unmaps == 0U);

    assert(qm_elf64_unload(&ops, NULL, &image) == QM_OK);
    assert(unmaps == 2U);

    /* Header and file bounds. */
    make_image(bytes, sizeof(bytes));
    ph = program_headers(bytes);
    ph[0].filesz = 5;
    ph[0].memsz = 4;
    expect_load_failure(bytes, QM_ERR_CORRUPT, 1);

    make_image(bytes, sizeof(bytes));
    eh = (struct test_ehdr *)bytes;
    eh->phoff = sizeof(*eh) - 1U;
    expect_load_failure(bytes, QM_ERR_CORRUPT, 1);

    make_image(bytes, sizeof(bytes));
    eh = (struct test_ehdr *)bytes;
    eh->ehsize--;
    expect_load_failure(bytes, QM_ERR_CORRUPT, 1);

    make_image(bytes, sizeof(bytes));
    eh = (struct test_ehdr *)bytes;
    eh->phentsize++;
    expect_load_failure(bytes, QM_ERR_CORRUPT, 1);

    make_image(bytes, sizeof(bytes));
    eh = (struct test_ehdr *)bytes;
    eh->ident[7] = 0x7f;
    expect_load_failure(bytes, QM_ERR_NOT_SUPPORTED, 1);

    make_image(bytes, sizeof(bytes));
    eh = (struct test_ehdr *)bytes;
    eh->flags = 1;
    expect_load_failure(bytes, QM_ERR_NOT_SUPPORTED, 1);

    make_image(bytes, sizeof(bytes));
    ph = program_headers(bytes);
    ph[0].offset = UINT64_MAX - 1U;
    ph[0].filesz = 4;
    expect_load_failure(bytes, QM_ERR_OVERFLOW, 1);

    make_image(bytes, sizeof(bytes));
    ph = program_headers(bytes);
    ph[0].offset = TEST_IMAGE_SIZE + 1U;
    expect_load_failure(bytes, QM_ERR_CORRUPT, 1);

    /* Unsupported program-header semantics and executable-stack policy. */
    make_image(bytes, sizeof(bytes));
    ph = program_headers(bytes);
    ph[1].type = TEST_PT_DYNAMIC;
    expect_load_failure(bytes, QM_ERR_NOT_SUPPORTED, 1);

    make_image(bytes, sizeof(bytes));
    ph = program_headers(bytes);
    ph[1].type = TEST_PT_INTERP;
    expect_load_failure(bytes, QM_ERR_NOT_SUPPORTED, 1);

    make_image(bytes, sizeof(bytes));
    ph = program_headers(bytes);
    ph[0].type = 0;
    ph[1].type = 0;
    expect_load_failure(bytes, QM_ERR_CORRUPT, 1);

    make_image(bytes, sizeof(bytes));
    ph = program_headers(bytes);
    ph[1].type = TEST_PT_GNU_RELRO;
    expect_load_failure(bytes, QM_ERR_NOT_SUPPORTED, 1);

    make_image(bytes, sizeof(bytes));
    ph = program_headers(bytes);
    ph[1].type = TEST_PT_GNU_STACK;
    ph[1].flags = TEST_PF_R | TEST_PF_W | TEST_PF_X;
    expect_load_failure(bytes, QM_ERR_PERMISSION, 1);

    /* Segment address, alignment, permissions, and entry-point constraints. */
    make_image(bytes, sizeof(bytes));
    ph = program_headers(bytes);
    ph[1].flags = TEST_PF_R | TEST_PF_W | TEST_PF_X;
    expect_load_failure(bytes, QM_ERR_PERMISSION, 1);

    make_image(bytes, sizeof(bytes));
    ph = program_headers(bytes);
    ph[1].vaddr = 0x40000200;
    expect_load_failure(bytes, QM_ERR_EXISTS, 1);

    make_image(bytes, sizeof(bytes));
    ph = program_headers(bytes);
    ph[0].align = 3;
    expect_load_failure(bytes, QM_ERR_CORRUPT, 1);

    make_image(bytes, sizeof(bytes));
    ph = program_headers(bytes);
    ph[0].vaddr = 0x40000181;
    expect_load_failure(bytes, QM_ERR_CORRUPT, 1);

    make_image(bytes, sizeof(bytes));
    eh = (struct test_ehdr *)bytes;
    eh->entry = 0x40001200;
    expect_load_failure(bytes, QM_ERR_CORRUPT, 1);

    make_image(bytes, sizeof(bytes));
    ph = program_headers(bytes);
    ph[0].vaddr = QM_ELF64_USER_MAX - 0x800U;
    ph[0].offset = 0x180;
    ph[0].memsz = 0x1000;
    ph[0].filesz = 4;
    expect_load_failure(bytes, QM_ERR_RANGE, 1);

    /* Mapping failure, NULL view, and rollback failure contracts. */
    make_image(bytes, sizeof(bytes));
    maps = unmaps = 0;
    fail_second_map = 1;
    assert(qm_elf64_load(bytes, sizeof(bytes), &ops, NULL, &image) ==
           QM_ERR_NO_MEMORY);
    assert(maps == 2U && unmaps == 1U);
    assert_image_zero(&image);
    fail_second_map = 0;

    make_image(bytes, sizeof(bytes));
    maps = unmaps = 0;
    null_first_view = 1;
    assert(qm_elf64_load(bytes, sizeof(bytes), &ops, NULL, &image) ==
           QM_ERR_NO_MEMORY);
    assert(maps == 1U && unmaps == 1U);
    assert_image_zero(&image);
    null_first_view = 0;

    make_image(bytes, sizeof(bytes));
    maps = unmaps = 0;
    fail_second_map = 1;
    fail_unmap = 1;
    assert(qm_elf64_load(bytes, sizeof(bytes), &ops, NULL, &image) == QM_ERR_IO);
    assert(maps == 2U && unmaps == 1U);
    assert_image_zero(&image);
    fail_second_map = 0;
    fail_unmap = 0;

    puts("ELF64 loader batch hardening and rollback tests: PASS");
    return 0;
}
