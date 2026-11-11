#include "qm_init.h"

#include <assert.h>
#include <stdio.h>

static int execution_order[16];
static size_t execution_count;

static int callback_early_a(void)
{
    execution_order[execution_count++] = 1;
    return 0;
}

static int callback_early_b(void)
{
    execution_order[execution_count++] = 2;
    return 0;
}

static int callback_core(void)
{
    execution_order[execution_count++] = 3;
    return 0;
}

static int callback_services(void)
{
    execution_order[execution_count++] = 4;
    return 0;
}

static int callback_late(void)
{
    execution_order[execution_count++] = 5;
    return 0;
}

static int callback_failure(void)
{
    execution_order[execution_count++] = 9;
    return -77;
}

static void test_normal_initialization(void)
{
    qm_init_state_t state;
    int callback_result = 0;

    qm_init_init();
    execution_count = 0;

    assert(qm_init_register(
        "early-b", QM_INIT_STAGE_EARLY, 20,
        callback_early_b) == QM_INIT_OK);

    assert(qm_init_register(
        "early-a", QM_INIT_STAGE_EARLY, 10,
        callback_early_a) == QM_INIT_OK);

    assert(qm_init_register(
        "core", QM_INIT_STAGE_CORE, 10,
        callback_core) == QM_INIT_OK);

    assert(qm_init_register(
        "services", QM_INIT_STAGE_SERVICES, 10,
        callback_services) == QM_INIT_OK);

    assert(qm_init_register(
        "late", QM_INIT_STAGE_LATE, 10,
        callback_late) == QM_INIT_OK);

    assert(qm_init_count() == 5);

    assert(qm_init_run_stage(QM_INIT_STAGE_CORE) ==
           QM_INIT_ERR_STAGE_ORDER);

    assert(qm_init_run_all() == QM_INIT_OK);

    assert(execution_count == 5);
    assert(execution_order[0] == 1);
    assert(execution_order[1] == 2);
    assert(execution_order[2] == 3);
    assert(execution_order[3] == 4);
    assert(execution_order[4] == 5);

    assert(qm_init_get_state("core", &state) == QM_INIT_OK);
    assert(state == QM_INIT_STATE_DONE);

    assert(qm_init_get_callback_result(
        "core", &callback_result) == QM_INIT_OK);
    assert(callback_result == 0);

    assert(qm_init_get_state("missing", &state) ==
           QM_INIT_ERR_NOT_FOUND);

    assert(qm_init_register(
        "too-late", QM_INIT_STAGE_EARLY, 0,
        callback_early_a) == QM_INIT_ERR_STAGE_ORDER);

    assert(qm_init_run_stage(QM_INIT_STAGE_LATE) ==
           QM_INIT_ERR_STAGE_ORDER);
}

static void test_validation(void)
{
    qm_init_init();

    assert(qm_init_register(
        "duplicate", QM_INIT_STAGE_EARLY, 0,
        callback_early_a) == QM_INIT_OK);

    assert(qm_init_register(
        "duplicate", QM_INIT_STAGE_EARLY, 1,
        callback_early_b) == QM_INIT_ERR_DUPLICATE);

    assert(qm_init_register(
        "", QM_INIT_STAGE_EARLY, 0,
        callback_early_a) == QM_INIT_ERR_INVALID);

    assert(qm_init_register(
        "null-callback", QM_INIT_STAGE_EARLY, 0,
        NULL) == QM_INIT_ERR_INVALID);

    assert(qm_init_register(
        "bad-stage", QM_INIT_STAGE_COUNT, 0,
        callback_early_a) == QM_INIT_ERR_INVALID);

    assert(qm_init_get_state(
        "duplicate", NULL) == QM_INIT_ERR_INVALID);
}

static void test_callback_failure(void)
{
    qm_init_state_t state;
    int callback_result = 0;

    qm_init_init();
    execution_count = 0;

    assert(qm_init_register(
        "early", QM_INIT_STAGE_EARLY, 0,
        callback_early_a) == QM_INIT_OK);

    assert(qm_init_register(
        "broken-core", QM_INIT_STAGE_CORE, 0,
        callback_failure) == QM_INIT_OK);

    assert(qm_init_register(
        "late", QM_INIT_STAGE_LATE, 0,
        callback_late) == QM_INIT_OK);

    assert(qm_init_run_stage(QM_INIT_STAGE_EARLY) == QM_INIT_OK);

    assert(qm_init_run_stage(QM_INIT_STAGE_CORE) ==
           QM_INIT_ERR_CALLBACK);

    assert(qm_init_get_state(
        "broken-core", &state) == QM_INIT_OK);
    assert(state == QM_INIT_STATE_FAILED);

    assert(qm_init_get_callback_result(
        "broken-core", &callback_result) == QM_INIT_OK);
    assert(callback_result == -77);

    assert(qm_init_run_stage(QM_INIT_STAGE_LATE) ==
           QM_INIT_ERR_STAGE_ORDER);

    assert(execution_count == 2);
    assert(execution_order[0] == 1);
    assert(execution_order[1] == 9);
}

int main(void)
{
    test_normal_initialization();
    test_validation();
    test_callback_failure();

    puts("[PASS] qm_init tests");
    return 0;
}
