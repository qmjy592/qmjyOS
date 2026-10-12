#ifndef QMJYOS_MEMORY_H
#define QMJYOS_MEMORY_H
#include <stddef.h>
#include <stdint.h>
#define MM_PAGE_SIZE 4096ULL
#define MM_MAX_RANGES 128U
#define MM_MAX_RESERVED 32U
enum mm_status { MM_OK=0, MM_ERR_ARGUMENT=-1, MM_ERR_FORMAT=-2, MM_ERR_CAPACITY=-3, MM_ERR_NO_MEMORY=-4 };
struct mm_range { uint64_t base, length; };
/* Parse Multiboot v1 memory-map entries (mmap_addr/mmap_length), not raw E820. */
int mm_parse_multiboot_mmap(uint32_t mmap_addr, uint32_t mmap_length);
int mm_init_from_multiboot(uint32_t magic, uint32_t info_addr, uint64_t kernel_start, uint64_t kernel_end);
int mm_reserve(uint64_t base, uint64_t length);
int mm_alloc_page(uint64_t *physical_address);
int mm_free_page(uint64_t physical_address);
/* Managed page count (excludes reserved pages; includes currently allocated pages). */
size_t mm_available_pages(void);
/* Current number of free pages. */
size_t mm_free_pages(void);
size_t mm_range_count(void);
/* Basic runtime test: alloc/free, alignment, count restoration, and double-free. */
int mm_self_test(void);
#endif
