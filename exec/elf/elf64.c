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
#define PT_INTERP 3U
#define PT_TLS 7U
#define PF_X 1U
#define PF_W 2U
#define PF_R 4U

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
    if (alignment == 0 || (alignment & (alignment - 1U)) != 0)
        return 0;
    if (add_overflow_u64(value, alignment - 1U, &sum))
        return 0;
    *out = sum & ~(alignment - 1U);
    return 1;
}

static void copy_bytes(void *destination, const void *source, qm_size_t size)
{
    qm_u8_t *d = (qm_u8_t *)destination;
    const qm_u8_t *s = (const qm_u8_t *)source;
    qm_size_t i;
    for (i = 0; i < size; ++i)
        d[i] = s[i];
}

static void zero_bytes(void *destination, qm_size_t size)
{
    qm_u8_t *d = (qm_u8_t *)destination;
    qm_size_t i;
    for (i = 0; i < size; ++i)
        d[i] = 0;
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
    struct elf64_ehdr *out_header
)
{
    const struct elf64_ehdr *header = out_header;
    qm_u64_t ph_bytes;
    qm_u64_t ph_end;

    if (file == NULL || out_header == NULL || file_size < sizeof(*header))
        return QM_ERR_INVALID;

    /* Copy bytes into an aligned local structure; input buffers need not be aligned. */
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
    if (header->type != ET_EXEC || header->machine != EM_X86_64 ||
        header->version != EV_CURRENT)
        return QM_ERR_NOT_SUPPORTED;
    if (header->ehsize < sizeof(*header) || header->ehsize > file_size ||
        header->phoff < header->ehsize ||
        header->phentsize < sizeof(struct elf64_phdr) ||
        header->phnum == 0 || header->phnum > 128U)
        return QM_ERR_CORRUPT;

    if (mul_overflow_u64(header->phentsize, header->phnum, &ph_bytes) ||
        add_overflow_u64(header->phoff, ph_bytes, &ph_end))
        return QM_ERR_OVERFLOW;
    if (header->phoff > file_size || ph_end > file_size)
        return QM_ERR_CORRUPT;

    return QM_OK;
}

qm_status_t qm_elf64_load(
    const void *file_bytes,
    qm_size_t file_size,
    const qm_elf64_memory_ops_t *memory_ops,
    void *memory_context,
    qm_elf64_image_t *out_image
)
{
    const qm_u8_t *file = (const qm_u8_t *)file_bytes;
    struct elf64_ehdr header_storage;
    const struct elf64_ehdr *header = &header_storage;
    qm_elf64_image_t image;
    void *views[QM_ELF64_MAX_SEGMENTS] = {0};
    qm_status_t status;
    qm_u64_t ph_table_end;
    int entry_is_executable = 0;
    qm_u32_t i;

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

    /* First pass: validate all load segments before invoking memory callbacks. */
    for (i = 0; i < header->phnum; ++i) {
        struct elf64_phdr ph_storage;
        const struct elf64_phdr *ph = &ph_storage;
        qm_elf64_segment_t *segment;
        qm_u64_t file_end;
        qm_u64_t memory_end;
        qm_u64_t page_offset;
        qm_u64_t rounded_size;
        qm_u64_t map_end;
        qm_u32_t protection = 0;
        qm_u32_t j;

        copy_bytes(&ph_storage,
                   file + header->phoff + (qm_u64_t)i * header->phentsize,
                   sizeof(ph_storage));
        if (ph->type == PT_INTERP || ph->type == PT_TLS)
            return QM_ERR_NOT_SUPPORTED;
        if (ph->type != PT_LOAD)
            continue;
        if (ph->filesz > ph->memsz)
            return QM_ERR_CORRUPT;
        if (ph->memsz == 0)
            continue;
        if (image.segment_count >= QM_ELF64_MAX_SEGMENTS)
            return QM_ERR_NO_SPACE;
        if (add_overflow_u64(ph->offset, ph->filesz, &file_end) ||
            file_end > file_size ||
            add_overflow_u64(ph->vaddr, ph->memsz, &memory_end))
            return QM_ERR_OVERFLOW;
        if (ph->vaddr < QM_ELF64_USER_MIN ||
            memory_end > QM_ELF64_USER_MAX || memory_end <= ph->vaddr)
            return QM_ERR_PERMISSION;
        if (ph->align > 1U &&
            (!is_power_of_two(ph->align) ||
             ((ph->vaddr - ph->offset) & (ph->align - 1U)) != 0))
            return QM_ERR_CORRUPT;
        if ((ph->vaddr & (QM_ELF64_PAGE_SIZE - 1U)) !=
            (ph->offset & (QM_ELF64_PAGE_SIZE - 1U)))
            return QM_ERR_CORRUPT;
        if ((ph->flags & ~(PF_R | PF_W | PF_X)) != 0)
            return QM_ERR_NOT_SUPPORTED;
        if ((ph->flags & (PF_W | PF_X)) == (PF_W | PF_X))
            return QM_ERR_PERMISSION;
        if (ph->flags & PF_R) protection |= QM_ELF64_PROT_READ;
        if (ph->flags & PF_W) protection |= QM_ELF64_PROT_WRITE;
        if (ph->flags & PF_X) protection |= QM_ELF64_PROT_EXEC;
        if ((protection & QM_ELF64_PROT_READ) == 0)
            return QM_ERR_PERMISSION;

        segment = &image.segments[image.segment_count];
        segment->virtual_address = ph->vaddr;
        segment->map_address = ph->vaddr & ~(QM_ELF64_PAGE_SIZE - 1U);
        page_offset = ph->vaddr - segment->map_address;
        if (add_overflow_u64(page_offset, ph->memsz, &rounded_size) ||
            !align_up_u64(rounded_size, QM_ELF64_PAGE_SIZE, &segment->map_size) ||
            add_overflow_u64(segment->map_address, segment->map_size, &map_end))
            return QM_ERR_OVERFLOW;
        if (map_end > QM_ELF64_USER_MAX)
            return QM_ERR_PERMISSION;

        segment->memory_size = ph->memsz;
        segment->file_size = ph->filesz;
        segment->file_offset = ph->offset;
        segment->protection = protection;

        if ((protection & QM_ELF64_PROT_EXEC) &&
            header->entry >= ph->vaddr && header->entry < memory_end)
            entry_is_executable = 1;

        for (j = 0; j < image.segment_count; ++j) {
            const qm_elf64_segment_t *previous = &image.segments[j];
            qm_u64_t previous_end;
            if (add_overflow_u64(previous->map_address, previous->map_size,
                                 &previous_end))
                return QM_ERR_CORRUPT;
            if (segment->map_address < previous_end &&
                previous->map_address < map_end)
                return QM_ERR_EXISTS;
        }
        ++image.segment_count;
    }

    if (image.segment_count == 0 || !entry_is_executable)
        return QM_ERR_CORRUPT;

    /* Second pass: map and populate. Roll back all prior mappings on failure. */
    for (i = 0; i < image.segment_count; ++i) {
        qm_elf64_segment_t *segment = &image.segments[i];
        qm_u64_t page_offset = segment->virtual_address - segment->map_address;
        qm_u64_t data_offset;

        status = memory_ops->map(memory_context, segment->map_address,
                                 segment->map_size, segment->protection,
                                 &views[i]);
        if (status != QM_OK || views[i] == NULL) {
            qm_status_t rollback_status;
            if (status == QM_OK && views[i] == NULL) {
                qm_status_t current_status = memory_ops->unmap(
                    memory_context, segment->map_address, segment->map_size);
                rollback_status = rollback_mappings(
                    memory_ops, memory_context, &image, i);
                if (current_status != QM_OK || rollback_status != QM_OK)
                    return QM_ERR_IO;
                return QM_ERR_NO_MEMORY;
            }
            rollback_status = rollback_mappings(
                memory_ops, memory_context, &image, i);
            return rollback_status == QM_OK ? status : QM_ERR_IO;
        }

        zero_bytes(views[i], (qm_size_t)segment->map_size);
        if (add_overflow_u64(page_offset, segment->file_size, &data_offset) ||
            data_offset > segment->map_size) {
            qm_status_t rollback_status = rollback_mappings(
                memory_ops, memory_context, &image, i + 1U);
            return rollback_status == QM_OK ? QM_ERR_OVERFLOW : QM_ERR_IO;
        }
        if (segment->file_size != 0)
            copy_bytes((qm_u8_t *)views[i] + page_offset,
                       file + segment->file_offset,
                       (qm_size_t)segment->file_size);
    }

    *out_image = image;
    return QM_OK;
}

qm_status_t qm_elf64_unload(
    const qm_elf64_memory_ops_t *memory_ops,
    void *memory_context,
    const qm_elf64_image_t *image
)
{
    qm_status_t first_error = QM_OK;
    qm_u32_t i;

    if (memory_ops == NULL || memory_ops->unmap == NULL || image == NULL ||
        image->segment_count == 0 ||
        image->segment_count > QM_ELF64_MAX_SEGMENTS)
        return QM_ERR_INVALID;

    for (i = image->segment_count; i > 0; --i) {
        const qm_elf64_segment_t *segment = &image->segments[i - 1U];
        qm_status_t status = memory_ops->unmap(
            memory_context, segment->map_address, segment->map_size);
        if (status != QM_OK && first_error == QM_OK)
            first_error = status;
    }
    return first_error;
}
