#include "qmjy/elf64.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define TEST_IMAGE_SIZE 1024U
#define TEST_MEMORY_BASE 0x10000ULL
#define TEST_MEMORY_SIZE 0x10000U
#define TEST_PT_LOAD 1U
#define TEST_PF_R 4U
#define TEST_PF_W 2U
#define TEST_PF_X 1U

struct test_ehdr {
    uint8_t ident[16]; uint16_t type, machine; uint32_t version;
    uint64_t entry, phoff, shoff; uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} __attribute__((packed));
struct test_phdr {
    uint32_t type, flags; uint64_t offset, vaddr, paddr, filesz, memsz, align;
} __attribute__((packed));

static uint8_t memory[TEST_MEMORY_SIZE];
static uint32_t maps;
static uint32_t unmaps;
static int fail_second_map;
static int fail_unmap;

static qm_status_t test_map(void *context, qm_u64_t base, qm_u64_t size,
                            qm_u32_t protection, void **view)
{
    (void)context;
    (void)protection;
    ++maps;
    if (fail_second_map && maps == 2)
        return QM_ERR_NO_MEMORY;
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

static void make_image(uint8_t *bytes, size_t length)
{
    struct test_ehdr *eh;
    struct test_phdr *ph;
    assert(length >= TEST_IMAGE_SIZE);
    for (size_t i = 0; i < length; ++i) bytes[i] = 0;
    eh = (struct test_ehdr *)bytes;
    eh->ident[0] = 0x7f; eh->ident[1] = 'E'; eh->ident[2] = 'L'; eh->ident[3] = 'F';
    eh->ident[4] = 2; eh->ident[5] = 1; eh->ident[6] = 1;
    eh->type = 2; eh->machine = 62; eh->version = 1;
    eh->entry = 0x10180; eh->phoff = sizeof(*eh);
    eh->ehsize = sizeof(*eh); eh->phentsize = sizeof(*ph); eh->phnum = 2;
    ph = (struct test_phdr *)(bytes + eh->phoff);
    ph[0].type = TEST_PT_LOAD; ph[0].flags = TEST_PF_R | TEST_PF_X;
    ph[0].offset = 0x180; ph[0].vaddr = 0x10180; ph[0].filesz = 4;
    ph[0].memsz = 4; ph[0].align = 0x1000;
    ph[1].type = TEST_PT_LOAD; ph[1].flags = TEST_PF_R | TEST_PF_W;
    ph[1].offset = 0x200; ph[1].vaddr = 0x11200; ph[1].filesz = 4;
    ph[1].memsz = 32; ph[1].align = 0x1000;
    bytes[0x180] = 0x90; bytes[0x181] = 0x90; bytes[0x182] = 0xC3;
    bytes[0x183] = 0x90;
    bytes[0x200] = 'D'; bytes[0x201] = 'A'; bytes[0x202] = 'T'; bytes[0x203] = 'A';
}

int main(void)
{
    uint8_t bytes[TEST_IMAGE_SIZE];
    uint8_t unaligned[TEST_IMAGE_SIZE + 1U];
    qm_elf64_image_t image;
    qm_elf64_memory_ops_t ops = { test_map, test_unmap };

    make_image(unaligned + 1U, TEST_IMAGE_SIZE);
    maps = unmaps = 0;
    assert(qm_elf64_load(unaligned + 1U, TEST_IMAGE_SIZE, &ops, NULL, &image) == QM_OK);
    assert(qm_elf64_unload(&ops, NULL, &image) == QM_OK);

    make_image(bytes, sizeof(bytes));
    maps = unmaps = 0;
    { qm_status_t rc = qm_elf64_load(bytes, sizeof(bytes), &ops, NULL, &image); assert(rc == QM_OK); }
    assert(image.entry == 0x10180 && image.segment_count == 2);
    assert(memory[0x180] == 0x90 && memory[0x181] == 0x90 && memory[0x182] == 0xC3);
    assert(memory[0x1200] == 'D' && memory[0x1201] == 'A');
    for (unsigned int i = 4; i < 32; ++i) assert(memory[0x1200 + i] == 0);
    assert(qm_elf64_unload(&ops, NULL, &image) == QM_OK);
    assert(unmaps == 2);

    make_image(bytes, sizeof(bytes));
    ((struct test_phdr *)(bytes + sizeof(struct test_ehdr)))[0].memsz = 0;
    ((struct test_phdr *)(bytes + sizeof(struct test_ehdr)))[0].filesz = 1;
    assert(qm_elf64_load(bytes, sizeof(bytes), &ops, NULL, &image) == QM_ERR_CORRUPT);

    make_image(bytes, sizeof(bytes));
    ((struct test_ehdr *)bytes)->phoff = sizeof(struct test_ehdr) - 1U;
    assert(qm_elf64_load(bytes, sizeof(bytes), &ops, NULL, &image) == QM_ERR_CORRUPT);

    make_image(bytes, sizeof(bytes));
    ((struct test_phdr *)(bytes + sizeof(struct test_ehdr)))[1].flags =
        TEST_PF_R | TEST_PF_W | TEST_PF_X;
    assert(qm_elf64_load(bytes, sizeof(bytes), &ops, NULL, &image) == QM_ERR_PERMISSION);

    make_image(bytes, sizeof(bytes));
    ((struct test_phdr *)(bytes + sizeof(struct test_ehdr)))[1].vaddr = 0x10200;
    assert(qm_elf64_load(bytes, sizeof(bytes), &ops, NULL, &image) == QM_ERR_EXISTS);

    make_image(bytes, sizeof(bytes));
    maps = unmaps = 0; fail_second_map = 1;
    assert(qm_elf64_load(bytes, sizeof(bytes), &ops, NULL, &image) == QM_ERR_NO_MEMORY);
    assert(maps == 2 && unmaps == 1);
    fail_second_map = 0;

    maps = unmaps = 0; fail_second_map = 1; fail_unmap = 1;
    assert(qm_elf64_load(bytes, sizeof(bytes), &ops, NULL, &image) == QM_ERR_IO);
    assert(maps == 2 && unmaps == 1);
    fail_second_map = 0; fail_unmap = 0;

    puts("ELF64 loader validation and rollback tests: PASS");
    return 0;
}
