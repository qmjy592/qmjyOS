#include "qm_cap.h"

#include <assert.h>
#include <stdio.h>

static void test_default_deny(void)
{
    qm_cap_init();

    assert(qm_cap_count() == 0);

    assert(qm_cap_check(
        1, 100, QM_CAP_RIGHT_READ) == QM_CAP_ERR_DENIED);
}

static void test_grant_and_check(void)
{
    qm_cap_rights_t rights = 0;

    qm_cap_init();

    assert(qm_cap_grant(
        1, 100, QM_CAP_RIGHT_READ) == QM_CAP_OK);

    assert(qm_cap_check(
        1, 100, QM_CAP_RIGHT_READ) == QM_CAP_OK);

    assert(qm_cap_check(
        1, 100, QM_CAP_RIGHT_WRITE) == QM_CAP_ERR_DENIED);

    assert(qm_cap_get(1, 100, &rights) == QM_CAP_OK);
    assert(rights == QM_CAP_RIGHT_READ);

    assert(qm_cap_count() == 1);
}

static void test_merge_and_revoke(void)
{
    qm_cap_rights_t rights = 0;

    qm_cap_init();

    assert(qm_cap_grant(
        2, 200, QM_CAP_RIGHT_READ) == QM_CAP_OK);

    assert(qm_cap_grant(
        2, 200, QM_CAP_RIGHT_WRITE | QM_CAP_RIGHT_EXEC)
        == QM_CAP_OK);

    assert(qm_cap_get(2, 200, &rights) == QM_CAP_OK);

    assert(rights == (
        QM_CAP_RIGHT_READ |
        QM_CAP_RIGHT_WRITE |
        QM_CAP_RIGHT_EXEC
    ));

    assert(qm_cap_check(
        2, 200,
        QM_CAP_RIGHT_READ | QM_CAP_RIGHT_WRITE)
        == QM_CAP_OK);

    assert(qm_cap_revoke(
        2, 200, QM_CAP_RIGHT_WRITE) == QM_CAP_OK);

    assert(qm_cap_check(
        2, 200, QM_CAP_RIGHT_WRITE) == QM_CAP_ERR_DENIED);

    assert(qm_cap_check(
        2, 200, QM_CAP_RIGHT_READ) == QM_CAP_OK);

    assert(qm_cap_revoke(
        2, 200, QM_CAP_RIGHT_READ | QM_CAP_RIGHT_EXEC)
        == QM_CAP_OK);

    assert(qm_cap_count() == 0);
}

static void test_subject_isolation(void)
{
    qm_cap_init();

    assert(qm_cap_grant(
        10, 300, QM_CAP_RIGHT_READ) == QM_CAP_OK);

    assert(qm_cap_check(
        10, 300, QM_CAP_RIGHT_READ) == QM_CAP_OK);

    assert(qm_cap_check(
        11, 300, QM_CAP_RIGHT_READ) == QM_CAP_ERR_DENIED);

    assert(qm_cap_check(
        10, 301, QM_CAP_RIGHT_READ) == QM_CAP_ERR_DENIED);
}

static void test_remove(void)
{
    qm_cap_init();

    assert(qm_cap_grant(
        3, 400, QM_CAP_RIGHT_READ | QM_CAP_RIGHT_WRITE)
        == QM_CAP_OK);

    assert(qm_cap_remove(3, 400) == QM_CAP_OK);
    assert(qm_cap_count() == 0);

    assert(qm_cap_get(
        3, 400, &(qm_cap_rights_t){0}) == QM_CAP_ERR_NOT_FOUND);

    assert(qm_cap_remove(3, 400) == QM_CAP_ERR_NOT_FOUND);
}

static void test_invalid_arguments(void)
{
    qm_cap_rights_t rights = 0;

    qm_cap_init();

    assert(qm_cap_grant(
        0, 100, QM_CAP_RIGHT_READ) == QM_CAP_ERR_INVALID);

    assert(qm_cap_grant(
        1, 0, QM_CAP_RIGHT_READ) == QM_CAP_ERR_INVALID);

    assert(qm_cap_grant(
        1, 100, 0) == QM_CAP_ERR_INVALID);

    assert(qm_cap_grant(
        1, 100, 0x80000000u) == QM_CAP_ERR_INVALID);

    assert(qm_cap_check(
        1, 100, 0) == QM_CAP_ERR_INVALID);

    assert(qm_cap_get(
        1, 100, NULL) == QM_CAP_ERR_INVALID);

    assert(qm_cap_revoke(
        1, 100, 0) == QM_CAP_ERR_INVALID);

    assert(qm_cap_remove(0, 100) == QM_CAP_ERR_INVALID);

    assert(rights == 0);
}

static void test_capacity(void)
{
    uint32_t i;

    qm_cap_init();

    for (i = 1; i <= QM_CAP_MAX_ENTRIES; ++i) {
        assert(qm_cap_grant(
            i, 500, QM_CAP_RIGHT_READ) == QM_CAP_OK);
    }

    assert(qm_cap_count() == QM_CAP_MAX_ENTRIES);

    assert(qm_cap_grant(
        QM_CAP_MAX_ENTRIES + 1, 500, QM_CAP_RIGHT_READ)
        == QM_CAP_ERR_FULL);

    assert(qm_cap_remove(1, 500) == QM_CAP_OK);

    assert(qm_cap_grant(
        QM_CAP_MAX_ENTRIES + 1, 500, QM_CAP_RIGHT_READ)
        == QM_CAP_OK);

    assert(qm_cap_count() == QM_CAP_MAX_ENTRIES);
}

int main(void)
{
    test_default_deny();
    test_grant_and_check();
    test_merge_and_revoke();
    test_subject_isolation();
    test_remove();
    test_invalid_arguments();
    test_capacity();

    puts("[PASS] qm_cap tests");
    return 0;
}
