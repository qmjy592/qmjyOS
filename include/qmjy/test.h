#ifndef QMJY_TEST_H
#define QMJY_TEST_H

#include "types.h"
#include "error.h"

typedef enum qm_test_result {
    QM_TEST_PASS = 0,
    QM_TEST_FAIL = 1,
    QM_TEST_SKIP = 2
} qm_test_result_t;

typedef qm_test_result_t (*qm_test_fn_t)(void);

typedef struct qm_test_case {
    const char *name;
    const char *description;
    qm_test_fn_t run;
} qm_test_case_t;

typedef struct qm_test_summary {
    qm_u32_t total;
    qm_u32_t passed;
    qm_u32_t failed;
    qm_u32_t skipped;
} qm_test_summary_t;

qm_status_t qm_test_register(
    const qm_test_case_t *test
);

qm_status_t qm_test_run(
    const char *name,
    qm_test_summary_t *summary
);

qm_status_t qm_test_run_all(
    qm_test_summary_t *summary
);

#endif
