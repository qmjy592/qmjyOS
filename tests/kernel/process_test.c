#include "qmjy/process.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    qm_elf64_image_t image = {0};
    qm_elf64_image_t invalid_image;
    qm_process_handle_t first = 0;
    qm_process_handle_t second = 0;
    qm_process_handle_t third = 0;
    qm_process_info_t info;

    image.entry = 0x400040;
    image.segment_count = 1;
    image.segments[0].virtual_address = 0x400000;
    image.segments[0].map_address = 0x400000;
    image.segments[0].map_size = 4096;
    image.segments[0].memory_size = 128;
    image.segments[0].file_size = 64;
    image.segments[0].protection = QM_ELF64_PROT_READ | QM_ELF64_PROT_EXEC;

    invalid_image = image;
    invalid_image.segments[0].map_address = QM_ELF64_USER_MAX - 0x1000U;
    invalid_image.segments[0].map_size = 0x2000U;

    assert(qm_process_table_init() == QM_OK);
    assert(qm_process_register_image("invalid", &invalid_image, &third) == QM_ERR_INVALID);
    assert(qm_process_register_image("first", &image, &first) == QM_OK);
    assert(qm_process_register_image("second", &image, &second) == QM_OK);
    assert(first != 0 && second != 0 && first != second);
    assert(qm_process_mark_running(first) == QM_OK);
    assert(qm_process_exit(first, 0) == QM_OK);
    assert(qm_process_reap(first) == QM_OK);

    /* A free earlier slot must not hide duplicate names in later slots. */
    assert(qm_process_register_image("second", &image, &third) == QM_ERR_EXISTS);
    assert(qm_process_register_image("third", &image, &third) == QM_OK);
    assert(third != first && third != second);

    assert(qm_process_query(second, &info) == QM_OK);
    assert(info.state == QM_PROCESS_READY && info.image.entry == image.entry);
    assert(qm_process_register_image("second", &image, &first) == QM_ERR_EXISTS);
    assert(qm_process_mark_running(second) == QM_OK);
    assert(qm_process_mark_running(second) == QM_ERR_STATE);
    assert(qm_process_exit(second, 7) == QM_OK);
    assert(qm_process_query(second, &info) == QM_OK);
    assert(info.state == QM_PROCESS_EXITED && info.exit_code == 7);
    assert(qm_process_reap(second) == QM_OK);
    assert(qm_process_query(second, &info) == QM_ERR_NOT_FOUND);

    puts("Process metadata lifecycle and validation tests: PASS");
    return 0;
}
