#include "qmjy/process_runtime.h"
#include "qmjy/heap.h"
#include "qmjy/user_access.h"
#include "memory.h"

#include <stddef.h>
#include <stdint.h>

#define QM_RUNTIME_SEGMENT_MAX QM_ELF64_MAX_SEGMENTS
#define QM_RUNTIME_STACK_PAGE (QM_PROCESS_RUNTIME_STACK_TOP - VM_PAGE_SIZE)
#define QM_RUNTIME_INITIAL_FLAGS (VM_USER | VM_WRITE | VM_NO_EXECUTE)

struct runtime_page {
    qm_u64_t virtual_address;
    qm_u64_t physical_address;
};

struct runtime_segment {
    qm_u64_t virtual_address;
    qm_u64_t size;
    void *staging;
};

struct runtime_record {
    qm_bool_t used;
    qm_bool_t address_space_valid;
    qm_process_handle_t id;
    qm_elf64_image_t image;
    vm_address_space_t address_space;
    qm_u64_t stack_pointer;
    struct runtime_page pages[QM_PROCESS_RUNTIME_MAX_PAGES];
    qm_u32_t page_count;
    struct runtime_segment segments[QM_RUNTIME_SEGMENT_MAX];
    qm_u32_t segment_count;
};

static struct runtime_record g_runtime[QM_PROCESS_RUNTIME_MAX];
static qm_bool_t g_runtime_initialized;
static int g_current_runtime = -1;

static void zero_bytes(void *memory, qm_size_t size)
{
    volatile qm_u8_t *bytes = (volatile qm_u8_t *)memory;
    qm_size_t i;
    for (i = 0; i < size; ++i)
        bytes[i] = 0;
}

static void copy_bytes(void *destination, const void *source, qm_size_t size)
{
    volatile qm_u8_t *dst = (volatile qm_u8_t *)destination;
    const volatile qm_u8_t *src = (const volatile qm_u8_t *)source;
    qm_size_t i;
    for (i = 0; i < size; ++i)
        dst[i] = src[i];
}

static struct runtime_record *find_runtime(qm_process_handle_t id)
{
    qm_u32_t i;
    if (id == 0)
        return NULL;
    for (i = 0; i < QM_PROCESS_RUNTIME_MAX; ++i) {
        if (g_runtime[i].used && g_runtime[i].id == id)
            return &g_runtime[i];
    }
    return NULL;
}

static struct runtime_record *find_free_runtime(void)
{
    qm_u32_t i;
    for (i = 0; i < QM_PROCESS_RUNTIME_MAX; ++i) {
        if (!g_runtime[i].used)
            return &g_runtime[i];
    }
    return NULL;
}

static int find_owned_page(const struct runtime_record *record,
                           qm_u64_t virtual_address)
{
    qm_u32_t i;
    for (i = 0; i < record->page_count; ++i) {
        if (record->pages[i].virtual_address == virtual_address)
            return (int)i;
    }
    return -1;
}

static void remove_owned_page(struct runtime_record *record, qm_u32_t index)
{
    if (index >= record->page_count)
        return;
    --record->page_count;
    if (index != record->page_count)
        record->pages[index] = record->pages[record->page_count];
    record->pages[record->page_count].virtual_address = 0;
    record->pages[record->page_count].physical_address = 0;
}

static qm_status_t append_owned_page(struct runtime_record *record,
                                     qm_u64_t virtual_address,
                                     qm_u64_t physical_address)
{
    if (record->page_count >= QM_PROCESS_RUNTIME_MAX_PAGES)
        return QM_ERR_NO_SPACE;
    if (find_owned_page(record, virtual_address) >= 0)
        return QM_ERR_EXISTS;
    record->pages[record->page_count].virtual_address = virtual_address;
    record->pages[record->page_count].physical_address = physical_address;
    ++record->page_count;
    return QM_OK;
}

static qm_status_t rollback_owned_suffix(struct runtime_record *record,
                                         qm_u32_t first_page)
{
    while (record->page_count > first_page) {
        qm_u32_t index = record->page_count - 1U;
        qm_u64_t virtual_address = record->pages[index].virtual_address;
        qm_u64_t physical_address = record->pages[index].physical_address;

        /*
         * A failed map may leave the last owned frame unmapped.  Only the
         * explicit NOT_MAPPED result permits freeing it without an unmap;
         * corruption/argument errors must stop rollback rather than silently
         * release a frame that may still be reachable through a damaged tree.
         */
        {
            qm_u64_t mapped_physical = 0;
            qm_u64_t mapped_flags = 0;
            int query_status = vm_address_space_query_page(
                &record->address_space, virtual_address,
                &mapped_physical, &mapped_flags);

            if (query_status == VM_OK) {
                if (mapped_physical != physical_address ||
                    !(mapped_flags & VM_PRESENT))
                    return QM_ERR_CORRUPT;
                if (vm_address_space_unmap_page(&record->address_space,
                                                virtual_address) != VM_OK)
                    return QM_ERR_IO;
            } else if (query_status != VM_ERR_NOT_MAPPED) {
                return QM_ERR_IO;
            }
        }
        if (mm_free_page(physical_address) != MM_OK)
            return QM_ERR_IO;
        remove_owned_page(record, index);
    }
    return QM_OK;
}

static int find_segment(const struct runtime_record *record,
                        qm_u64_t virtual_address, qm_u64_t size)
{
    qm_u32_t i;
    for (i = 0; i < record->segment_count; ++i) {
        if (record->segments[i].virtual_address == virtual_address &&
            record->segments[i].size == size)
            return (int)i;
    }
    return -1;
}

/*
 * ELF memory callback. Every segment is initially mapped USER|RW|NX so the
 * kernel can populate it safely. A private staging view satisfies the legacy
 * ELF loader's contiguous writable-view contract; bytes are committed to the
 * segment's owned physical pages after the complete ELF image validates.
 */
static qm_status_t runtime_map_segment(void *context,
                                       qm_u64_t virtual_base,
                                       qm_u64_t size,
                                       qm_u32_t protection,
                                       void **writable_view)
{
    struct runtime_record *record = (struct runtime_record *)context;
    void *staging;
    qm_u64_t offset;
    qm_u32_t start_page_count;
    qm_u32_t start_segment_count;

    (void)protection;
    if (writable_view != NULL)
        *writable_view = NULL;
    if (record == NULL || writable_view == NULL || size == 0 ||
        (virtual_base & (VM_PAGE_SIZE - 1U)) != 0 ||
        (size & (VM_PAGE_SIZE - 1U)) != 0 ||
        size > (qm_u64_t)(QM_PROCESS_RUNTIME_MAX_PAGES * VM_PAGE_SIZE))
        return QM_ERR_INVALID;
    if (record->segment_count >= QM_RUNTIME_SEGMENT_MAX ||
        size / VM_PAGE_SIZE >
            QM_PROCESS_RUNTIME_MAX_PAGES - record->page_count)
        return QM_ERR_NO_SPACE;
    if (find_segment(record, virtual_base, size) >= 0)
        return QM_ERR_EXISTS;

    staging = kmalloc((qm_size_t)size);
    if (staging == NULL)
        return QM_ERR_NO_MEMORY;

    start_page_count = record->page_count;
    start_segment_count = record->segment_count;
    for (offset = 0; offset < size; offset += VM_PAGE_SIZE) {
        qm_u64_t physical = 0;
        qm_u64_t virtual_address = virtual_base + offset;
        qm_status_t ownership_status;

        if (mm_alloc_page(&physical) != MM_OK) {
            qm_status_t rollback_status =
                rollback_owned_suffix(record, start_page_count);
            (void)kfree(staging);
            return rollback_status == QM_OK ? QM_ERR_NO_MEMORY : QM_ERR_IO;
        }
        ownership_status = append_owned_page(record, virtual_address, physical);
        if (ownership_status != QM_OK) {
            if (mm_free_page(physical) != MM_OK) {
                (void)kfree(staging);
                return QM_ERR_IO;
            }
            (void)rollback_owned_suffix(record, start_page_count);
            (void)kfree(staging);
            return ownership_status;
        }
        if (vm_address_space_map_page(&record->address_space,
                                      virtual_address, physical,
                                      QM_RUNTIME_INITIAL_FLAGS) != VM_OK) {
            qm_status_t rollback_status =
                rollback_owned_suffix(record, start_page_count);
            (void)kfree(staging);
            return rollback_status == QM_OK ? QM_ERR_NO_MEMORY : QM_ERR_IO;
        }
    }

    record->segments[start_segment_count].virtual_address = virtual_base;
    record->segments[start_segment_count].size = size;
    record->segments[start_segment_count].staging = staging;
    record->segment_count++;
    *writable_view = staging;
    return QM_OK;
}

static qm_status_t runtime_unmap_segment(void *context,
                                         qm_u64_t virtual_base,
                                         qm_u64_t size)
{
    struct runtime_record *record = (struct runtime_record *)context;
    int segment_index;
    qm_u64_t offset;

    if (record == NULL)
        return QM_ERR_INVALID;
    segment_index = find_segment(record, virtual_base, size);
    if (segment_index < 0)
        return QM_ERR_NOT_FOUND;

    /* Validate ownership and mappings before the first destructive operation. */
    for (offset = 0; offset < size; offset += VM_PAGE_SIZE) {
        qm_u64_t pa = 0;
        qm_u64_t flags = 0;
        qm_u64_t va = virtual_base + offset;
        int owned_index = find_owned_page(record, va);
        if (owned_index < 0 ||
            vm_address_space_query_page(&record->address_space, va,
                                        &pa, &flags) != VM_OK ||
            !(flags & VM_PRESENT) ||
            pa != record->pages[owned_index].physical_address)
            return QM_ERR_CORRUPT;
    }

    for (offset = 0; offset < size; offset += VM_PAGE_SIZE) {
        qm_u64_t va = virtual_base + offset;
        int owned_index = find_owned_page(record, va);
        qm_u64_t pa;
        qm_u64_t flags;
        if (owned_index < 0 ||
            vm_address_space_query_page(&record->address_space, va,
                                        &pa, &flags) != VM_OK ||
            pa != record->pages[owned_index].physical_address ||
            vm_address_space_unmap_page(&record->address_space, va) != VM_OK)
            return QM_ERR_IO;
        if (mm_free_page(record->pages[owned_index].physical_address) != MM_OK)
            return QM_ERR_IO;
        remove_owned_page(record, (qm_u32_t)owned_index);
    }

    if (kfree(record->segments[segment_index].staging) != QM_OK)
        return QM_ERR_IO;
    --record->segment_count;
    if ((qm_u32_t)segment_index != record->segment_count)
        record->segments[segment_index] = record->segments[record->segment_count];
    record->segments[record->segment_count].virtual_address = 0;
    record->segments[record->segment_count].size = 0;
    record->segments[record->segment_count].staging = NULL;
    return QM_OK;
}

static qm_status_t commit_loaded_segments(struct runtime_record *record)
{
    qm_u32_t i;

    for (i = 0; i < record->segment_count; ++i) {
        struct runtime_segment *runtime_segment = &record->segments[i];
        const qm_elf64_segment_t *image_segment = NULL;
        qm_u32_t j;
        qm_u64_t offset;
        qm_u64_t final_flags = VM_USER;

        for (j = 0; j < record->image.segment_count; ++j) {
            if (record->image.segments[j].map_address ==
                    runtime_segment->virtual_address &&
                record->image.segments[j].map_size == runtime_segment->size) {
                image_segment = &record->image.segments[j];
                break;
            }
        }
        if (image_segment == NULL || runtime_segment->staging == NULL)
            return QM_ERR_CORRUPT;

        for (offset = 0; offset < runtime_segment->size;
             offset += VM_PAGE_SIZE) {
            int index = find_owned_page(record,
                                        runtime_segment->virtual_address + offset);
            if (index < 0)
                return QM_ERR_CORRUPT;
            copy_bytes((void *)(uintptr_t)record->pages[index].physical_address,
                       (const qm_u8_t *)runtime_segment->staging +
                           (qm_size_t)offset,
                       (qm_size_t)VM_PAGE_SIZE);
        }

        if (image_segment->protection & QM_ELF64_PROT_WRITE)
            final_flags |= VM_WRITE;
        if (!(image_segment->protection & QM_ELF64_PROT_EXEC))
            final_flags |= VM_NO_EXECUTE;

        for (offset = 0; offset < runtime_segment->size;
             offset += VM_PAGE_SIZE) {
            if (vm_address_space_protect_page(
                    &record->address_space,
                    runtime_segment->virtual_address + offset,
                    final_flags) != VM_OK)
                return QM_ERR_IO;
        }

        if (kfree(runtime_segment->staging) != QM_OK)
            return QM_ERR_IO;
        runtime_segment->staging = NULL;
    }
    return QM_OK;
}

static qm_status_t allocate_user_stack(struct runtime_record *record)
{
    qm_u64_t physical = 0;
    qm_status_t status;

    if (record->page_count >= QM_PROCESS_RUNTIME_MAX_PAGES)
        return QM_ERR_NO_SPACE;
    if (mm_alloc_page(&physical) != MM_OK)
        return QM_ERR_NO_MEMORY;
    zero_bytes((void *)(uintptr_t)physical, (qm_size_t)VM_PAGE_SIZE);
    if (vm_address_space_map_page(&record->address_space,
                                  QM_RUNTIME_STACK_PAGE, physical,
                                  VM_USER | VM_WRITE | VM_NO_EXECUTE) != VM_OK) {
        (void)mm_free_page(physical);
        return QM_ERR_NO_MEMORY;
    }
    status = append_owned_page(record, QM_RUNTIME_STACK_PAGE, physical);
    if (status != QM_OK) {
        if (vm_address_space_unmap_page(&record->address_space,
                                        QM_RUNTIME_STACK_PAGE) != VM_OK)
            return QM_ERR_IO;
        if (mm_free_page(physical) != MM_OK)
            return QM_ERR_IO;
        return status;
    }
    record->stack_pointer = QM_PROCESS_RUNTIME_STACK_TOP - 16U;
    return QM_OK;
}

static qm_status_t destroy_runtime_resources(struct runtime_record *record)
{
    qm_u32_t i;

    if (record->address_space_valid) {
        if (vm_current_root_physical() == record->address_space.root_physical) {
            if (vm_activate() != VM_OK)
                return QM_ERR_BUSY;
            if (g_current_runtime >= 0 &&
                &g_runtime[g_current_runtime] == record)
                g_current_runtime = -1;
        }
        if (vm_address_space_destroy(&record->address_space) != VM_OK)
            return QM_ERR_IO;
        record->address_space_valid = QM_FALSE;
    }

    while (record->page_count != 0) {
        qm_u32_t index = record->page_count - 1U;
        if (mm_free_page(record->pages[index].physical_address) != MM_OK)
            return QM_ERR_IO;
        remove_owned_page(record, index);
    }
    for (i = 0; i < record->segment_count; ++i) {
        if (record->segments[i].staging != NULL) {
            if (kfree(record->segments[i].staging) != QM_OK)
                return QM_ERR_IO;
            record->segments[i].staging = NULL;
        }
    }
    record->segment_count = 0;
    return QM_OK;
}

qm_status_t qm_process_runtime_init(void)
{
    qm_u32_t i;

    if (g_runtime_initialized)
        return QM_ERR_BUSY;
    {
        qm_heap_stats_t heap_stats;
        if (vm_root_physical() == 0 ||
            qm_heap_get_stats(&heap_stats) != QM_OK)
            return QM_ERR_STATE;
    }
    if (qm_process_table_init() != QM_OK)
        return QM_ERR_STATE;
    for (i = 0; i < QM_PROCESS_RUNTIME_MAX; ++i)
        zero_bytes(&g_runtime[i], sizeof(g_runtime[i]));
    g_current_runtime = -1;
    g_runtime_initialized = QM_TRUE;
    return QM_OK;
}

qm_status_t qm_process_runtime_load(const char *name,
                                    const void *elf_bytes,
                                    qm_size_t elf_size,
                                    qm_process_handle_t *out_id)
{
    static const qm_elf64_memory_ops_t memory_ops = {
        runtime_map_segment,
        runtime_unmap_segment
    };
    struct runtime_record *record;
    qm_elf64_image_t image;
    qm_process_handle_t id = 0;
    qm_status_t status;

    if (!g_runtime_initialized || name == NULL || elf_bytes == NULL ||
        elf_size == 0 || out_id == NULL || g_current_runtime >= 0)
        return QM_ERR_INVALID;
    record = find_free_runtime();
    if (record == NULL)
        return QM_ERR_NO_SPACE;
    zero_bytes(record, sizeof(*record));
    if (vm_address_space_create(&record->address_space) != VM_OK)
        return QM_ERR_NO_MEMORY;
    record->address_space_valid = QM_TRUE;

    status = qm_elf64_load(elf_bytes, elf_size, &memory_ops, record, &image);
    if (status != QM_OK) {
        qm_status_t cleanup_status = destroy_runtime_resources(record);
        zero_bytes(record, sizeof(*record));
        return cleanup_status == QM_OK ? status : QM_ERR_IO;
    }
    record->image = image;

    status = commit_loaded_segments(record);
    if (status != QM_OK) {
        qm_status_t cleanup_status = destroy_runtime_resources(record);
        zero_bytes(record, sizeof(*record));
        return cleanup_status == QM_OK ? status : QM_ERR_IO;
    }

    status = allocate_user_stack(record);
    if (status != QM_OK) {
        qm_status_t cleanup_status = destroy_runtime_resources(record);
        zero_bytes(record, sizeof(*record));
        return cleanup_status == QM_OK ? status : QM_ERR_IO;
    }

    status = qm_process_register_image(name, &record->image, &id);
    if (status != QM_OK) {
        qm_status_t cleanup_status = destroy_runtime_resources(record);
        zero_bytes(record, sizeof(*record));
        return cleanup_status == QM_OK ? status : QM_ERR_IO;
    }

    record->id = id;
    record->used = QM_TRUE;
    *out_id = id;
    return QM_OK;
}

qm_status_t qm_process_runtime_query(qm_process_handle_t id,
                                     qm_process_runtime_info_t *out_info)
{
    struct runtime_record *record;
    qm_process_info_t process_info;

    if (out_info == NULL)
        return QM_ERR_INVALID;
    record = find_runtime(id);
    if (record == NULL)
        return QM_ERR_NOT_FOUND;
    if (qm_process_query(id, &process_info) != QM_OK)
        return QM_ERR_NOT_FOUND;

    out_info->id = id;
    out_info->root_physical = record->address_space.root_physical;
    out_info->entry = record->image.entry;
    out_info->user_stack_pointer = record->stack_pointer;
    out_info->owned_page_count = record->page_count;
    out_info->state = process_info.state;
    return QM_OK;
}

qm_status_t qm_process_runtime_query_page(qm_process_handle_t id,
                                          qm_u64_t virtual_address,
                                          qm_u64_t *physical_address,
                                          qm_u64_t *flags)
{
    struct runtime_record *record = find_runtime(id);
    int status;
    if (record == NULL)
        return QM_ERR_NOT_FOUND;
    status = vm_address_space_query_page(&record->address_space,
                                         virtual_address,
                                         physical_address, flags);
    return status == VM_OK ? QM_OK : QM_ERR_NOT_FOUND;
}

qm_status_t qm_process_runtime_user_range_check(qm_process_handle_t id,
                                                qm_u64_t user_address,
                                                qm_size_t length,
                                                int write_access)
{
    struct runtime_record *record = find_runtime(id);
    if (record == NULL)
        return QM_ERR_NOT_FOUND;
    return qm_user_range_check(&record->address_space, user_address,
                               length, write_access);
}

qm_status_t qm_process_runtime_activate(qm_process_handle_t id)
{
    struct runtime_record *record = find_runtime(id);
    qm_status_t status;

    if (record == NULL)
        return QM_ERR_NOT_FOUND;
    if (g_current_runtime >= 0 || !record->address_space_valid)
        return QM_ERR_BUSY;
    status = (qm_status_t)vm_address_space_activate(&record->address_space);
    if (status != QM_OK)
        return QM_ERR_IO;
    status = qm_process_mark_running(id);
    if (status != QM_OK) {
        (void)vm_activate();
        return status;
    }
    g_current_runtime = (int)(record - g_runtime);
    return QM_OK;
}

qm_status_t qm_process_runtime_exit_current(qm_s32_t exit_code)
{
    struct runtime_record *record;
    qm_status_t status;

    if (g_current_runtime < 0 ||
        (qm_u32_t)g_current_runtime >= QM_PROCESS_RUNTIME_MAX)
        return QM_ERR_STATE;
    record = &g_runtime[g_current_runtime];
    if (!record->used ||
        vm_current_root_physical() != record->address_space.root_physical)
        return QM_ERR_STATE;

    /* Switch to the shared kernel root before making the process reapable. */
    if (vm_activate() != VM_OK)
        return QM_ERR_IO;
    status = qm_process_exit(record->id, exit_code);
    if (status != QM_OK) {
        (void)vm_address_space_activate(&record->address_space);
        return status;
    }
    g_current_runtime = -1;
    return QM_OK;
}

qm_status_t qm_process_runtime_terminate(qm_process_handle_t id,
                                         qm_s32_t exit_code)
{
    struct runtime_record *record = find_runtime(id);
    qm_process_info_t process_info;

    if (record == NULL)
        return QM_ERR_NOT_FOUND;
    if (qm_process_query(id, &process_info) != QM_OK)
        return QM_ERR_NOT_FOUND;
    if (process_info.state == QM_PROCESS_EXITED)
        return QM_OK;
    if (process_info.state != QM_PROCESS_READY &&
        process_info.state != QM_PROCESS_RUNNING)
        return QM_ERR_STATE;

    if (vm_current_root_physical() == record->address_space.root_physical) {
        if (vm_activate() != VM_OK)
            return QM_ERR_IO;
    }
    if (g_current_runtime >= 0 &&
        &g_runtime[g_current_runtime] == record)
        g_current_runtime = -1;
    return qm_process_exit(id, exit_code);
}

qm_status_t qm_process_runtime_reap(qm_process_handle_t id)
{
    struct runtime_record *record = find_runtime(id);
    qm_process_info_t process_info;
    qm_status_t status;

    if (record == NULL)
        return QM_ERR_NOT_FOUND;
    if (qm_process_query(id, &process_info) != QM_OK)
        return QM_ERR_NOT_FOUND;
    if (process_info.state != QM_PROCESS_EXITED)
        return QM_ERR_BUSY;

    status = destroy_runtime_resources(record);
    if (status != QM_OK)
        return status;
    status = qm_process_reap(id);
    if (status != QM_OK)
        return status;
    zero_bytes(record, sizeof(*record));
    return QM_OK;
}
