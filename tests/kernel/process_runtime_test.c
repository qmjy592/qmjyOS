#include "qmjy/process_runtime.h"
#include "qmjy/heap.h"
#include "qmjy/user_access.h"
#include "paging.h"
#include "memory.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define TEST_FILE_SIZE 0x1020U
#define TEST_PT_LOAD 1U
#define TEST_PT_DYNAMIC 2U
#define TEST_PF_R 4U
#define TEST_PF_W 2U
#define TEST_PF_X 1U
#define TEST_PAGE_COUNT 512U

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

static uint8_t physical_pages[TEST_PAGE_COUNT][4096]
    __attribute__((aligned(4096)));
static uint8_t physical_used[TEST_PAGE_COUNT];
static size_t physical_free_count = TEST_PAGE_COUNT;

int mm_alloc_page(uint64_t *physical_address)
{
    size_t i;
    if (physical_address == NULL)
        return MM_ERR_ARGUMENT;
    for (i = 0; i < TEST_PAGE_COUNT; ++i) {
        if (!physical_used[i]) {
            physical_used[i] = 1;
            --physical_free_count;
            *physical_address = (uint64_t)(uintptr_t)physical_pages[i];
            return MM_OK;
        }
    }
    return MM_ERR_NO_MEMORY;
}

int mm_free_page(uint64_t physical_address)
{
    uintptr_t base = (uintptr_t)&physical_pages[0][0];
    uintptr_t end = (uintptr_t)&physical_pages[TEST_PAGE_COUNT][0];
    size_t index;
    if (physical_address < base || physical_address >= end ||
        ((physical_address - base) & 4095U) != 0)
        return MM_ERR_ARGUMENT;
    index = (size_t)((physical_address - base) / 4096U);
    if (!physical_used[index])
        return MM_ERR_ARGUMENT;
    physical_used[index] = 0;
    ++physical_free_count;
    return MM_OK;
}

size_t mm_free_pages(void)
{
    return physical_free_count;
}

static void make_elf(uint8_t *bytes)
{
    struct test_ehdr *eh = (struct test_ehdr *)bytes;
    struct test_phdr *ph;
    size_t i;

    for (i = 0; i < TEST_FILE_SIZE; ++i)
        bytes[i] = 0;
    eh->ident[0] = 0x7f;
    eh->ident[1] = 'E';
    eh->ident[2] = 'L';
    eh->ident[3] = 'F';
    eh->ident[4] = 2;
    eh->ident[5] = 1;
    eh->ident[6] = 1;
    eh->type = 2;
    eh->machine = 62;
    eh->version = 1;
    eh->entry = 0x40000000ULL;
    eh->phoff = sizeof(*eh);
    eh->ehsize = sizeof(*eh);
    eh->phentsize = sizeof(*ph);
    eh->phnum = 2;

    ph = (struct test_phdr *)(bytes + eh->phoff);
    ph[0].type = TEST_PT_LOAD;
    ph[0].flags = TEST_PF_R | TEST_PF_X;
    ph[0].offset = 0x1000;
    ph[0].vaddr = 0x40000000ULL;
    ph[0].filesz = 4;
    ph[0].memsz = 16;
    ph[0].align = 0x1000;

    ph[1].type = TEST_PT_LOAD;
    ph[1].flags = TEST_PF_R | TEST_PF_W;
    ph[1].offset = 0x1010;
    ph[1].vaddr = 0x40001010ULL;
    ph[1].filesz = 4;
    ph[1].memsz = 64;
    ph[1].align = 0x1000;

    bytes[0x1000] = 0x90;
    bytes[0x1001] = 0x90;
    bytes[0x1002] = 0x90;
    bytes[0x1003] = 0xC3;
    bytes[0x1010] = 'D';
    bytes[0x1011] = 'A';
    bytes[0x1012] = 'T';
    bytes[0x1013] = 'A';
}

int main(void)
{
    uint8_t elf[TEST_FILE_SIZE];
    uint8_t bad_elf[TEST_FILE_SIZE];
    struct test_ehdr *bad_eh;
    struct test_phdr *bad_ph;
    qm_process_handle_t first = 0;
    qm_process_handle_t second = 0;
    qm_process_runtime_info_t first_info;
    qm_process_runtime_info_t second_info;
    qm_process_info_t process_info;
    uint64_t first_code_pa = 0;
    uint64_t second_code_pa = 0;
    uint64_t flags = 0;
    size_t initial_free;

    assert(qm_heap_init() == QM_OK);
    assert(vm_init() == VM_OK);
    assert(qm_process_runtime_init() == QM_OK);
    initial_free = mm_free_pages();

    make_elf(elf);
    assert(qm_process_runtime_load("runtime-one", elf, sizeof(elf),
                                   &first) == QM_OK);
    assert(qm_process_runtime_load("runtime-two", elf, sizeof(elf),
                                   &second) == QM_OK);
    assert(first != 0 && second != 0 && first != second);

    assert(qm_process_runtime_query(first, &first_info) == QM_OK);
    assert(qm_process_runtime_query(second, &second_info) == QM_OK);
    assert(first_info.root_physical != 0 && second_info.root_physical != 0);
    assert(first_info.root_physical != second_info.root_physical);
    assert(first_info.entry == 0x40000000ULL);
    assert(first_info.owned_page_count == 3U);
    assert(first_info.user_stack_pointer == QM_PROCESS_RUNTIME_STACK_TOP - 16U);

    assert(qm_process_runtime_query_page(first, 0x40000000ULL,
                                         &first_code_pa, &flags) == QM_OK);
    assert((flags & (VM_USER | VM_WRITE | VM_NO_EXECUTE)) == VM_USER);
    assert(qm_process_runtime_query_page(second, 0x40000000ULL,
                                         &second_code_pa, &flags) == QM_OK);
    assert(first_code_pa != second_code_pa);
    assert((flags & (VM_USER | VM_WRITE | VM_NO_EXECUTE)) == VM_USER);

    assert(qm_process_runtime_user_range_check(first, 0x40000000ULL,
                                               4, 0) == QM_OK);
    assert(qm_process_runtime_user_range_check(first, 0x40000000ULL,
                                               4, 1) == QM_ERR_PERMISSION);
    assert(qm_process_runtime_user_range_check(first, 0x40001010ULL,
                                               32, 1) == QM_OK);
    assert(qm_process_runtime_user_range_check(first,
                first_info.user_stack_pointer - 8U, 8, 1) == QM_OK);
    assert(qm_process_runtime_user_range_check(first, 0x200000ULL,
                                               4, 0) == QM_ERR_PERMISSION);
    assert(qm_process_runtime_user_range_check(first, UINT64_MAX - 3U,
                                               16, 0) != QM_OK);

    assert(qm_process_runtime_activate(first) == QM_OK);
    assert(vm_current_root_physical() == first_info.root_physical);
    assert(qm_process_runtime_exit_current(17) == QM_OK);
    assert(vm_current_root_physical() == vm_root_physical());
    assert(qm_process_query(first, &process_info) == QM_OK);
    assert(process_info.state == QM_PROCESS_EXITED);
    assert(process_info.exit_code == 17);
    assert(qm_process_runtime_reap(first) == QM_OK);
    assert(mm_free_pages() == initial_free - 3U);

    assert(qm_process_runtime_activate(second) == QM_OK);
    assert(vm_current_root_physical() == second_info.root_physical);
    assert(qm_process_runtime_exit_current(0) == QM_OK);
    assert(qm_process_runtime_reap(second) == QM_OK);
    assert(mm_free_pages() == initial_free);
    assert(vm_tables_used() == 3U);

    /* A READY process can be terminated without ever activating its CR3. */
    assert(qm_process_runtime_load("runtime-terminate-ready", elf,
                                   sizeof(elf), &first) == QM_OK);
    assert(qm_process_runtime_terminate(first, -9) == QM_OK);
    assert(vm_current_root_physical() == vm_root_physical());
    assert(qm_process_query(first, &process_info) == QM_OK);
    assert(process_info.state == QM_PROCESS_EXITED);
    assert(process_info.exit_code == -9);
    assert(qm_process_runtime_reap(first) == QM_OK);
    assert(mm_free_pages() == initial_free);
    assert(vm_tables_used() == 3U);

    /* A mid-load physical-page shortage must rollback the first segment. */
    {
        uint64_t held[TEST_PAGE_COUNT - 1U];
        size_t held_count = 0;
        while (held_count < TEST_PAGE_COUNT - 1U &&
               mm_alloc_page(&held[held_count]) == MM_OK)
            ++held_count;
        assert(held_count == TEST_PAGE_COUNT - 1U);
        assert(mm_free_pages() == 1U);
        assert(qm_process_runtime_load("runtime-shortage", elf, sizeof(elf),
                                       &first) == QM_ERR_NO_MEMORY);
        assert(mm_free_pages() == 1U);
        while (held_count != 0) {
            --held_count;
            assert(mm_free_page(held[held_count]) == MM_OK);
        }
        assert(mm_free_pages() == initial_free);
        assert(vm_tables_used() == 3U);
    }

    /* PT_DYNAMIC is rejected before the runtime commits any segment pages. */
    make_elf(bad_elf);
    bad_eh = (struct test_ehdr *)bad_elf;
    bad_ph = (struct test_phdr *)(bad_elf + bad_eh->phoff);
    bad_ph[1].type = TEST_PT_DYNAMIC;
    assert(qm_process_runtime_load("runtime-bad", bad_elf,
                                   sizeof(bad_elf), &first) ==
           QM_ERR_NOT_SUPPORTED);
    assert(mm_free_pages() == initial_free);
    assert(vm_tables_used() == 3U);

    puts("ELF-to-process private address-space runtime tests: PASS");
    return 0;
}
