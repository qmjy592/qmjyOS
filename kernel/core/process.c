#include "qmjy/process.h"

#include <stddef.h>

struct process_record {
    qm_process_info_t info;
};

static struct process_record g_processes[QM_PROCESS_MAX];
static qm_bool_t g_initialized;
static qm_process_handle_t g_next_id = 1;

static qm_size_t string_length(const char *text)
{
    qm_size_t length = 0;
    if (text == NULL)
        return 0;
    while (text[length] != '\0')
        ++length;
    return length;
}

static void string_copy(char *destination, const char *source, qm_size_t size)
{
    qm_size_t i = 0;
    if (size == 0)
        return;
    while (i + 1U < size && source[i] != '\0') {
        destination[i] = source[i];
        ++i;
    }
    destination[i] = '\0';
}

static void zero_bytes(void *memory, qm_size_t size)
{
    qm_u8_t *bytes = (qm_u8_t *)memory;
    qm_size_t i;
    for (i = 0; i < size; ++i)
        bytes[i] = 0;
}

static int image_valid(const qm_elf64_image_t *image)
{
    qm_u32_t i;
    int entry_found = 0;
    if (image == NULL || image->segment_count == 0 ||
        image->segment_count > QM_ELF64_MAX_SEGMENTS)
        return 0;
    for (i = 0; i < image->segment_count; ++i) {
        const qm_elf64_segment_t *segment = &image->segments[i];
        qm_u64_t end;
        qm_u64_t map_end;
        qm_u64_t offset;
        qm_u32_t j;
        if (segment->map_size == 0 || segment->memory_size == 0 ||
            segment->map_address < QM_ELF64_USER_MIN ||
            segment->map_size > QM_ELF64_USER_MAX ||
            segment->map_address > QM_ELF64_USER_MAX - segment->map_size ||
            (segment->map_address & (QM_ELF64_PAGE_SIZE - 1U)) != 0 ||
            (segment->map_size & (QM_ELF64_PAGE_SIZE - 1U)) != 0 ||
            segment->virtual_address < segment->map_address ||
            segment->file_size > segment->memory_size ||
            (segment->protection & ~(QM_ELF64_PROT_READ | QM_ELF64_PROT_WRITE |
                                     QM_ELF64_PROT_EXEC)) != 0 ||
            (segment->protection & QM_ELF64_PROT_READ) == 0)
            return 0;
        map_end = segment->map_address + segment->map_size;
        offset = segment->virtual_address - segment->map_address;
        if (offset > segment->map_size ||
            segment->memory_size > segment->map_size - offset)
            return 0;
        end = segment->virtual_address + segment->memory_size;
        if (end < segment->virtual_address || end > map_end)
            return 0;
        if ((segment->protection & (QM_ELF64_PROT_WRITE | QM_ELF64_PROT_EXEC)) ==
            (QM_ELF64_PROT_WRITE | QM_ELF64_PROT_EXEC))
            return 0;
        if ((segment->protection & QM_ELF64_PROT_EXEC) &&
            image->entry >= segment->virtual_address && image->entry < end)
            entry_found = 1;
        for (j = 0; j < i; ++j) {
            const qm_elf64_segment_t *previous = &image->segments[j];
            qm_u64_t previous_end = previous->map_address + previous->map_size;
            if (segment->map_address < previous_end &&
                previous->map_address < map_end)
                return 0;
        }
    }
    return entry_found;
}

static struct process_record *find_process(qm_process_handle_t id)
{
    qm_u32_t i;
    if (id == 0)
        return NULL;
    for (i = 0; i < QM_PROCESS_MAX; ++i) {
        if (g_processes[i].info.state != QM_PROCESS_FREE &&
            g_processes[i].info.id == id)
            return &g_processes[i];
    }
    return NULL;
}

qm_status_t qm_process_table_init(void)
{
    qm_u32_t i;
    if (g_initialized) {
        for (i = 0; i < QM_PROCESS_MAX; ++i) {
            if (g_processes[i].info.state != QM_PROCESS_FREE)
                return QM_ERR_BUSY;
        }
    }
    for (i = 0; i < QM_PROCESS_MAX; ++i)
        zero_bytes(&g_processes[i], sizeof(g_processes[i]));
    g_initialized = QM_TRUE;
    if (g_next_id == 0)
        g_next_id = 1;
    return QM_OK;
}

qm_status_t qm_process_register_image(
    const char *name,
    const qm_elf64_image_t *image,
    qm_process_handle_t *out_id
)
{
    qm_u32_t i;
    qm_size_t length;
    struct process_record *slot = NULL;

    if (!g_initialized || name == NULL || out_id == NULL || !image_valid(image))
        return QM_ERR_INVALID;
    length = string_length(name);
    if (length == 0 || length >= QM_PROCESS_NAME_MAX)
        return QM_ERR_INVALID;

    for (i = 0; i < QM_PROCESS_MAX; ++i) {
        if (g_processes[i].info.state == QM_PROCESS_FREE) {
            if (slot == NULL)
                slot = &g_processes[i];
            continue;
        }
        if (g_processes[i].info.name[0] != '\0') {
            qm_size_t j = 0;
            int equal = 1;
            while (name[j] != '\0' || g_processes[i].info.name[j] != '\0') {
                if (name[j] != g_processes[i].info.name[j]) {
                    equal = 0;
                    break;
                }
                ++j;
            }
            if (equal && g_processes[i].info.state != QM_PROCESS_EXITED)
                return QM_ERR_EXISTS;
        }
    }
    if (slot == NULL)
        return QM_ERR_NO_SPACE;
    if (g_next_id == 0 || g_next_id == (qm_process_handle_t)-1)
        return QM_ERR_OVERFLOW;

    zero_bytes(&slot->info, sizeof(slot->info));
    slot->info.id = g_next_id++;
    slot->info.state = QM_PROCESS_READY;
    slot->info.exit_code = 0;
    string_copy(slot->info.name, name, sizeof(slot->info.name));
    slot->info.image = *image;
    *out_id = slot->info.id;
    return QM_OK;
}

qm_status_t qm_process_mark_running(qm_process_handle_t id)
{
    struct process_record *process;
    if (!g_initialized)
        return QM_ERR_STATE;
    process = find_process(id);
    if (process == NULL)
        return QM_ERR_NOT_FOUND;
    if (process->info.state != QM_PROCESS_READY)
        return QM_ERR_STATE;
    process->info.state = QM_PROCESS_RUNNING;
    return QM_OK;
}

qm_status_t qm_process_exit(qm_process_handle_t id, qm_s32_t exit_code)
{
    struct process_record *process;
    if (!g_initialized)
        return QM_ERR_STATE;
    process = find_process(id);
    if (process == NULL)
        return QM_ERR_NOT_FOUND;
    if (process->info.state != QM_PROCESS_READY &&
        process->info.state != QM_PROCESS_RUNNING)
        return QM_ERR_STATE;
    process->info.exit_code = exit_code;
    process->info.state = QM_PROCESS_EXITED;
    return QM_OK;
}

qm_status_t qm_process_reap(qm_process_handle_t id)
{
    struct process_record *process;
    if (!g_initialized)
        return QM_ERR_STATE;
    process = find_process(id);
    if (process == NULL)
        return QM_ERR_NOT_FOUND;
    if (process->info.state != QM_PROCESS_EXITED)
        return QM_ERR_BUSY;
    zero_bytes(&process->info, sizeof(process->info));
    return QM_OK;
}

qm_status_t qm_process_query(qm_process_handle_t id, qm_process_info_t *out_info)
{
    struct process_record *process;
    qm_u8_t *destination;
    const qm_u8_t *source;
    qm_size_t i;
    if (out_info == NULL)
        return QM_ERR_INVALID;
    if (!g_initialized)
        return QM_ERR_STATE;
    process = find_process(id);
    if (process == NULL)
        return QM_ERR_NOT_FOUND;
    destination = (qm_u8_t *)out_info;
    source = (const qm_u8_t *)&process->info;
    for (i = 0; i < sizeof(*out_info); ++i)
        destination[i] = source[i];
    return QM_OK;
}
