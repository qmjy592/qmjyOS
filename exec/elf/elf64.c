#include "qmjy/elf64.h"

#include <stddef.h>
#include <stdint.h>

#define ELF_MAGIC0 0x7FU
#define ELF_MAGIC1 'E'
#define ELF_MAGIC2 'L'
#define ELF_MAGIC3 'F'
#define ELFCLASS64 2U
#define ELFDATA2LSB 1U
#define EV_CURRENT 1U
#define ET_EXEC 2U
#define EM_X86_64 62U
#define PT_LOAD 1U
#define PT_DYNAMIC 2U
#define PT_INTERP 3U
#define PT_SHLIB 5U
#define PT_TLS 7U
#define PT_GNU_STACK 0x6474E551U
#define PT_GNU_RELRO 0x6474E552U
#define PF_X 1U
#define PF_W 2U
#define PF_R 4U

#define QMJY_ELF64_HARDENING_V1 1

struct elf64_ehdr {
    qm_u8_t ident[16];
    qm_u16_t type;
    qm_u16_t machine;
    qm_u32_t version;
    qm_u64_t entry;
    qm_u64_t phoff;
    qm_u64_t shoff;
    qm_u32_t flags;
    qm_u16_t ehsize;
    qm_u16_t phentsize;
    qm_u16_t phnum;
    qm_u16_t shentsize;
    qm_u16_t shnum;
    qm_u16_t shstrndx;
} __attribute__((packed));

struct elf64_phdr {
    qm_u32_t type;
    qm_u32_t flags;
    qm_u64_t offset;
    qm_u64_t vaddr;
    qm_u64_t paddr;
    qm_u64_t filesz;
    qm_u64_t memsz;
    qm_u64_t align;
} __attribute__((packed));

static int add_overflow_u64(qm_u64_t a, qm_u64_t b, qm_u64_t *out)
{
    if (a > UINT64_MAX - b)
        return 1;
    *out = a + b;
    return 0;
}

static int mul_overflow_u64(qm_u64_t a, qm_u64_t b, qm_u64_t *out)
{
    if (b != 0 && a > UINT64_MAX / b)
        return 1;
    *out = a * b;
    return 0;
}

static int is_power_of_two(qm_u64_t value)
{
    return value != 0 && (value & (value - 1U)) == 0;
}

static int align_up_u64(qm_u64_t value, qm_u64_t alignment, qm_u64_t *out)
{
    qm_u64_t sum;

    if (!is_power_of_two(alignment) ||
        add_overflow_u64(value, alignment - 1U, &sum))
        return 0;
    *out = sum & ~(alignment - 1U);
    return 1;
}

static void copy_bytes(void *destination, const void *source, qm_size_t size)
{
    qm_u8_t *dst = (qm_u8_t *)destination;
    const qm_u8_t *src = (const qm_u8_t *)source;
    qm_size_t i;

    for (i = 0; i < size; ++i)
        dst[i] = src[i];
}

static void zero_bytes(void *destination, qm_size_t size)
{
    qm_u8_t *dst = (qm_u8_t *)destination;
    qm_size_t i;

    for (i = 0; i < size; ++i)
        dst[i] = 0;
}

static int segment_is_zero(const qm_elf64_segment_t *segment)
{
    return segment->virtual_address == 0 && segment->map_address == 0 &&
           segment->map_size == 0 && segment->memory_size == 0 &&
           segment->file_size == 0 && segment->file_offset == 0 &&
           segment->protection == 0 && segment->reserved == 0;
}

/*
 * One canonical descriptor validator is shared by the load and unload paths.
 * The file-specific bounds are checked while parsing PT_LOAD; this helper
 * validates every invariant that can be reconstructed from the descriptor.
 */
static qm_status_t validate_image_descriptor(const qm_elf64_image_t *image)
{
    int entry_is_executable = 0;
    qm_u32_t i;

    if (image == NULL)
        return QM_ERR_INVALID;
    if (image->segment_count == 0 ||
        image->segment_count > QM_ELF64_MAX_SEGMENTS)
        return QM_ERR_CORRUPT;
    if (image->reserved != 0)
        return QM_ERR_CORRUPT;

    for (i = 0; i < image->segment_count; ++i) {
        const qm_elf64_segment_t *segment = &image->segments[i];
        qm_u64_t map_end;
        qm_u64_t memory_end;
        qm_u64_t file_end;
        qm_u64_t page_offset;
        qm_u64_t rounded_memory;
        qm_u64_t expected_map_size;
        qm_u32_t j;

        if (segment->reserved != 0 || segment->map_size == 0 ||
            segment->memory_size == 0 ||
            segment->file_size > segment->memory_size ||
            (segment->map_address & (QM_ELF64_PAGE_SIZE - 1U)) != 0 ||
            (segment->map_size & (QM_ELF64_PAGE_SIZE - 1U)) != 0 ||
            segment->virtual_address < segment->map_address)
            return QM_ERR_CORRUPT;

        page_offset = segment->virtual_address - segment->map_address;
        if (page_offset >= QM_ELF64_PAGE_SIZE ||
            segment->map_address !=
                (segment->virtual_address & ~(QM_ELF64_PAGE_SIZE - 1U)))
            return QM_ERR_CORRUPT;

        if (add_overflow_u64(segment->map_address, segment->map_size,
                             &map_end) ||
            add_overflow_u64(segment->virtual_address, segment->memory_size,
                             &memory_end) ||
            add_overflow_u64(segment->file_offset, segment->file_size,
                             &file_end) ||
            add_overflow_u64(page_offset, segment->memory_size,
                             &rounded_memory))
            return QM_ERR_OVERFLOW;
        (void)file_end;

        if (!align_up_u64(rounded_memory, QM_ELF64_PAGE_SIZE,
                          &expected_map_size))
            return QM_ERR_OVERFLOW;
        if (expected_map_size != segment->map_size ||
            memory_end <= segment->virtual_address || memory_end > map_end)
            return QM_ERR_CORRUPT;
        if (segment->map_address < QM_ELF64_USER_MIN ||
            map_end > QM_ELF64_USER_MAX)
            return QM_ERR_RANGE;
        if (segment->map_size > (qm_u64_t)SIZE_MAX)
            return QM_ERR_RANGE;

        if ((segment->protection &
             ~(QM_ELF64_PROT_READ | QM_ELF64_PROT_WRITE |
               QM_ELF64_PROT_EXEC)) != 0 ||
            (segment->protection & QM_ELF64_PROT_READ) == 0)
            return QM_ERR_PERMISSION;
        if ((segment->protection &
             (QM_ELF64_PROT_WRITE | QM_ELF64_PROT_EXEC)) ==
            (QM_ELF64_PROT_WRITE | QM_ELF64_PROT_EXEC))
            return QM_ERR_PERMISSION;

        if ((segment->protection & QM_ELF64_PROT_EXEC) != 0 &&
            image->entry >= segment->virtual_address &&
            image->entry < memory_end)
            entry_is_executable = 1;

        for (j = 0; j < i; ++j) {
            const qm_elf64_segment_t *previous = &image->segments[j];
            qm_u64_t previous_end;

            if (add_overflow_u64(previous->map_address,
                                 previous->map_size, &previous_end))
                return QM_ERR_CORRUPT;
            if (segment->map_address < previous_end &&
                previous->map_address < map_end)
                return QM_ERR_EXISTS;
        }
    }

    for (i = image->segment_count; i < QM_ELF64_MAX_SEGMENTS; ++i) {
        if (!segment_is_zero(&image->segments[i]))
            return QM_ERR_CORRUPT;
    }

    return entry_is_executable ? QM_OK : QM_ERR_CORRUPT;
}

static qm_status_t rollback_mappings(
    const qm_elf64_memory_ops_t *memory_ops,
    void *memory_context,
    const qm_elf64_image_t *image,
    qm_u32_t count)
{
    qm_status_t first_error = QM_OK;

    while (count > 0) {
        const qm_elf64_segment_t *segment = &image->segments[--count];
        qm_status_t status = memory_ops->unmap(
            memory_context, segment->map_address, segment->map_size);

        if (status != QM_OK && first_error == QM_OK)
            first_error = status;
    }
    return first_error;
}

static qm_status_t validate_header(
    const qm_u8_t *file,
    qm_size_t file_size,
    struct elf64_ehdr *out_header)
{
    const struct elf64_ehdr *header = out_header;
    qm_u64_t ph_bytes;
    qm_u64_t ph_end;
    qm_u32_t i;

    if (file == NULL || out_header == NULL || file_size < sizeof(*header))
        return QM_ERR_INVALID;

    /* Copy into an aligned local structure; the input buffer may be unaligned. */
    copy_bytes(out_header, file, sizeof(*out_header));

    if (header->ident[0] != ELF_MAGIC0 ||
        header->ident[1] != ELF_MAGIC1 ||
        header->ident[2] != ELF_MAGIC2 ||
        header->ident[3] != ELF_MAGIC3)
        return QM_ERR_CORRUPT;
    if (header->ident[4] != ELFCLASS64 ||
        header->ident[5] != ELFDATA2LSB ||
        header->ident[6] != EV_CURRENT)
        return QM_ERR_NOT_SUPPORTED;
    if ((header->ident[7] != 0U && header->ident[7] != 3U) ||
        header->ident[8] != 0U)
        return QM_ERR_NOT_SUPPORTED;
    for (i = 9; i < 16U; ++i) {
        if (header->ident[i] != 0U)
            return QM_ERR_CORRUPT;
    }

    if (header->type != ET_EXEC || header->machine != EM_X86_64 ||
        header->version != EV_CURRENT)
        return QM_ERR_NOT_SUPPORTED;
    if (header->flags != 0U)
        return QM_ERR_NOT_SUPPORTED;
    if (header->ehsize != sizeof(*header) ||
        header->phentsize != sizeof(struct elf64_phdr) ||
        header->phoff < header->ehsize || header->phnum == 0U ||
        header->phnum > 128U)
        return QM_ERR_CORRUPT;

    if (mul_overflow_u64(header->phentsize, header->phnum, &ph_bytes) ||
        add_overflow_u64(header->phoff, ph_bytes, &ph_end))
        return QM_ERR_OVERFLOW;
    if (header->phoff > (qm_u64_t)file_size ||
        ph_end > (qm_u64_t)file_size)
        return QM_ERR_CORRUPT;

    return QM_OK;
}

qm_status_t qm_elf64_load(
    const void *file_bytes,
    qm_size_t file_size,
    const qm_elf64_memory_ops_t *memory_ops,
    void *memory_context,
    qm_elf64_image_t *out_image)
{
    const qm_u8_t *file = (const qm_u8_t *)file_bytes;
    struct elf64_ehdr header_storage;
    const struct elf64_ehdr *header = &header_storage;
    qm_elf64_image_t image;
    void *views[QM_ELF64_MAX_SEGMENTS] = {0};
    qm_status_t status;
    qm_u64_t ph_table_end;
    qm_u32_t i;

    /* Always leave a deterministic empty output on every load failure. */
    if (out_image != NULL)
        zero_bytes(out_image, sizeof(*out_image));
    if (memory_ops == NULL || memory_ops->map == NULL ||
        memory_ops->unmap == NULL || out_image == NULL)
        return QM_ERR_INVALID;

    zero_bytes(&image, sizeof(image));
    status = validate_header(file, file_size, &header_storage);
    if (status != QM_OK)
        return status;

    if (mul_overflow_u64(header->phentsize, header->phnum, &ph_table_end) ||
        add_overflow_u64(header->phoff, ph_table_end, &ph_table_end))
        return QM_ERR_OVERFLOW;
    image.entry = header->entry;

    /* Pass one: parse and validate every program header before mapping memory. */
    for (i = 0; i < header->phnum; ++i) {
        struct elf64_phdr ph_storage;
        const struct elf64_phdr *ph = &ph_storage;
        qm_elf64_segment_t *segment;
        qm_u64_t ph_offset;
        qm_u64_t file_end;
        qm_u64_t memory_end;
        qm_u64_t page_offset;
        qm_u64_t rounded_size;
        qm_u64_t map_end;
        qm_u32_t protection = 0;

        if (mul_overflow_u64(i, header->phentsize, &ph_offset) ||
            add_overflow_u64(header->phoff, ph_offset, &ph_offset) ||
            ph_offset > ph_table_end ||
            (qm_u64_t)sizeof(ph_storage) > ph_table_end - ph_offset)
            return QM_ERR_CORRUPT;
        copy_bytes(&ph_storage, file + (qm_size_t)ph_offset,
                   sizeof(ph_storage));

        if (ph->type == PT_DYNAMIC || ph->type == PT_INTERP ||
            ph->type == PT_SHLIB || ph->type == PT_TLS ||
            ph->type == PT_GNU_RELRO)
            return QM_ERR_NOT_SUPPORTED;
        if (ph->type == PT_GNU_STACK) {
            if ((ph->flags & ~(PF_R | PF_W | PF_X)) != 0)
                return QM_ERR_NOT_SUPPORTED;
            if ((ph->flags & PF_X) != 0)
                return QM_ERR_PERMISSION;
            continue;
        }
        if (ph->type != PT_LOAD)
            continue;

        if (ph->filesz > ph->memsz)
            return QM_ERR_CORRUPT;
        if (add_overflow_u64(ph->offset, ph->filesz, &file_end))
            return QM_ERR_OVERFLOW;
        if (file_end > (qm_u64_t)file_size)
            return QM_ERR_CORRUPT;
        if (ph->memsz == 0)
            continue;
        if (image.segment_count >= QM_ELF64_MAX_SEGMENTS)
            return QM_ERR_NO_SPACE;
        if (add_overflow_u64(ph->vaddr, ph->memsz, &memory_end))
            return QM_ERR_OVERFLOW;
        if (ph->vaddr < QM_ELF64_USER_MIN ||
            memory_end > QM_ELF64_USER_MAX || memory_end <= ph->vaddr)
            return QM_ERR_RANGE;
        if (ph->align > 1U &&
            (!is_power_of_two(ph->align) ||
             (ph->vaddr & (ph->align - 1U)) !=
                 (ph->offset & (ph->align - 1U))))
            return QM_ERR_CORRUPT;
        if ((ph->vaddr & (QM_ELF64_PAGE_SIZE - 1U)) !=
            (ph->offset & (QM_ELF64_PAGE_SIZE - 1U)))
            return QM_ERR_CORRUPT;
        if ((ph->flags & ~(PF_R | PF_W | PF_X)) != 0)
            return QM_ERR_NOT_SUPPORTED;
        if ((ph->flags & (PF_W | PF_X)) == (PF_W | PF_X))
            return QM_ERR_PERMISSION;
        if ((ph->flags & PF_R) != 0)
            protection |= QM_ELF64_PROT_READ;
        if ((ph->flags & PF_W) != 0)
            protection |= QM_ELF64_PROT_WRITE;
        if ((ph->flags & PF_X) != 0)
            protection |= QM_ELF64_PROT_EXEC;
        if ((protection & QM_ELF64_PROT_READ) == 0)
            return QM_ERR_PERMISSION;

        segment = &image.segments[image.segment_count];
        segment->virtual_address = ph->vaddr;
        segment->map_address = ph->vaddr & ~(QM_ELF64_PAGE_SIZE - 1U);
        page_offset = ph->vaddr - segment->map_address;
        if (add_overflow_u64(page_offset, ph->memsz, &rounded_size) ||
            !align_up_u64(rounded_size, QM_ELF64_PAGE_SIZE,
                          &segment->map_size) ||
            add_overflow_u64(segment->map_address, segment->map_size,
                             &map_end))
            return QM_ERR_OVERFLOW;
        if (map_end > QM_ELF64_USER_MAX ||
            segment->map_address < QM_ELF64_USER_MIN ||
            segment->map_size > (qm_u64_t)SIZE_MAX)
            return QM_ERR_RANGE;

        segment->memory_size = ph->memsz;
        segment->file_size = ph->filesz;
        segment->file_offset = ph->offset;
        segment->protection = protection;
        ++image.segment_count;
    }

    status = validate_image_descriptor(&image);
    if (status != QM_OK)
        return status;

    /* Pass two: map and populate. A failed map callback must not map its range. */
    for (i = 0; i < image.segment_count; ++i) {
        qm_elf64_segment_t *segment = &image.segments[i];
        qm_u64_t page_offset = segment->virtual_address - segment->map_address;

        status = memory_ops->map(memory_context, segment->map_address,
                                 segment->map_size, segment->protection,
                                 &views[i]);
        if (status != QM_OK) {
            qm_status_t rollback_status = rollback_mappings(
                memory_ops, memory_context, &image, i);
            return rollback_status == QM_OK ? status : QM_ERR_IO;
        }
        if (views[i] == NULL) {
            qm_status_t current_status = memory_ops->unmap(
                memory_context, segment->map_address, segment->map_size);
            qm_status_t rollback_status = rollback_mappings(
                memory_ops, memory_context, &image, i);
            if (current_status != QM_OK || rollback_status != QM_OK)
                return QM_ERR_IO;
            return QM_ERR_NO_MEMORY;
        }

        zero_bytes(views[i], (qm_size_t)segment->map_size);
        if (segment->file_size != 0) {
            copy_bytes((qm_u8_t *)views[i] + (qm_size_t)page_offset,
                       file + (qm_size_t)segment->file_offset,
                       (qm_size_t)segment->file_size);
        }
    }

    *out_image = image;
    return QM_OK;
}

qm_status_t qm_elf64_unload(
    const qm_elf64_memory_ops_t *memory_ops,
    void *memory_context,
    const qm_elf64_image_t *image)
{
    qm_status_t first_error = QM_OK;
    qm_status_t status;
    qm_u32_t i;

    if (memory_ops == NULL || memory_ops->unmap == NULL || image == NULL)
        return QM_ERR_INVALID;

    status = validate_image_descriptor(image);
    if (status != QM_OK)
        return status;

    /* Complete descriptor validation precedes the first destructive callback. */
    for (i = image->segment_count; i > 0; --i) {
        const qm_elf64_segment_t *segment = &image->segments[i - 1U];
        status = memory_ops->unmap(memory_context, segment->map_address,
                                   segment->map_size);
        if (status != QM_OK && first_error == QM_OK)
            first_error = status;
    }
    return first_error;
}
