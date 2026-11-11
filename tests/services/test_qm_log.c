#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../services/logging/qm_log.h"

static int failures;

#define CHECK(expr) do { \
    if (!(expr)) { \
        printf("[FAIL] %s:%d: %s\n", \
               __FILE__, __LINE__, #expr); \
        ++failures; \
    } else { \
        printf("[PASS] %s\n", #expr); \
    } \
} while (0)

int main(void)
{
    qm_log_record_t record;
    size_t i;

    qm_log_init();

    CHECK(qm_log_count() == 0);
    CHECK(qm_log_read(&record) == -2);

    CHECK(qm_log_write(
        QM_LOG_INFO, "kernel", "system started"
    ) == 0);

    CHECK(qm_log_write(
        QM_LOG_ERROR, "memory", "allocation failed"
    ) == 0);

    CHECK(qm_log_count() == 2);

    CHECK(qm_log_read(&record) == 0);
    CHECK(record.sequence == 1);
    CHECK(record.level == QM_LOG_INFO);
    CHECK(strcmp(record.tag, "kernel") == 0);
    CHECK(strcmp(record.message, "system started") == 0);

    CHECK(qm_log_read(&record) == 0);
    CHECK(record.sequence == 2);
    CHECK(record.level == QM_LOG_ERROR);

    CHECK(qm_log_read(&record) == -2);

    CHECK(qm_log_write(
        (qm_log_level_t)99, "test", "invalid"
    ) == -1);

    CHECK(qm_log_write(
        QM_LOG_INFO, NULL, "invalid"
    ) == -1);

    qm_log_init();

    for (i = 0; i < QM_LOG_CAPACITY + 5; ++i) {
        CHECK(qm_log_write(
            QM_LOG_DEBUG, "test", "ring buffer"
        ) == 0);
    }

    CHECK(qm_log_count() == QM_LOG_CAPACITY);
    CHECK(qm_log_dropped() == 5);

    CHECK(qm_log_read(&record) == 0);
    CHECK(record.sequence == 6);

    CHECK(strcmp(
        qm_log_level_name(QM_LOG_WARN), "WARN"
    ) == 0);

    if (failures) {
        printf("\n%d test(s) failed.\n", failures);
        return EXIT_FAILURE;
    }

    printf("\nAll logging tests passed.\n");
    return EXIT_SUCCESS;
}
