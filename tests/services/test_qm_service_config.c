#include "qm_service_config.h"

#include <assert.h>
#include <stdio.h>

static int dummy_start(void)
{
    return 0;
}

static int dummy_stop(void)
{
    return 0;
}

static const char *const dep_a[] = {"service-a"};
static const char *const dep_b[] = {"service-b"};
static const char *const dep_missing[] = {"not-present"};
static const char *const dep_duplicate[] = {
    "service-a", "service-a"
};

static void test_valid_configuration(void)
{
    qm_service_config_entry_t entries[] = {
        {
            "service-b", dummy_start, dummy_stop,
            dep_a, 1, 1
        },
        {
            "service-a", dummy_start, dummy_stop,
            NULL, 0, 1
        },
        {
            "disabled-service", dummy_start, dummy_stop,
            NULL, 0, 0
        }
    };

    assert(qm_service_config_validate(
        entries, 3) == QM_SERVICE_CONFIG_OK);
}

static void test_missing_dependency(void)
{
    qm_service_config_entry_t entries[] = {
        {
            "service-a", dummy_start, dummy_stop,
            dep_missing, 1, 1
        }
    };

    assert(qm_service_config_validate(
        entries, 1) == QM_SERVICE_CONFIG_ERR_DEPENDENCY);
}

static void test_cycle_detection(void)
{
    qm_service_config_entry_t entries[] = {
        {
            "service-a", dummy_start, dummy_stop,
            dep_b, 1, 1
        },
        {
            "service-b", dummy_start, dummy_stop,
            dep_a, 1, 1
        }
    };

    assert(qm_service_config_validate(
        entries, 2) == QM_SERVICE_CONFIG_ERR_CYCLE);
}

static void test_duplicate_entries(void)
{
    qm_service_config_entry_t entries[] = {
        {
            "service-a", dummy_start, dummy_stop,
            NULL, 0, 1
        },
        {
            "service-a", dummy_start, dummy_stop,
            NULL, 0, 1
        }
    };

    assert(qm_service_config_validate(
        entries, 2) == QM_SERVICE_CONFIG_ERR_DUPLICATE);
}

static void test_duplicate_dependencies(void)
{
    qm_service_config_entry_t entries[] = {
        {
            "service-a", dummy_start, dummy_stop,
            NULL, 0, 1
        },
        {
            "service-b", dummy_start, dummy_stop,
            dep_duplicate, 2, 1
        }
    };

    assert(qm_service_config_validate(
        entries, 2) == QM_SERVICE_CONFIG_ERR_DUPLICATE);
}

static void test_invalid_entries(void)
{
    qm_service_config_entry_t no_start[] = {
        {
            "service-a", NULL, dummy_stop,
            NULL, 0, 1
        }
    };

    assert(qm_service_config_validate(
        no_start, 1) == QM_SERVICE_CONFIG_ERR_INVALID);

    assert(qm_service_config_validate(
        NULL, 1) == QM_SERVICE_CONFIG_ERR_INVALID);

    assert(qm_service_config_validate(
        NULL, 0) == QM_SERVICE_CONFIG_OK);

    assert(qm_service_config_validate(
        no_start, QM_SERVICE_CONFIG_MAX + 1)
        == QM_SERVICE_CONFIG_ERR_FULL);
}

static void test_apply(void)
{
    qm_service_state_t state;

    qm_service_config_entry_t entries[] = {
        {
            "config-a", dummy_start, dummy_stop,
            NULL, 0, 1
        },
        {
            "config-b", dummy_start, dummy_stop,
            dep_a, 1, 1
        }
    };

    /*
     * Use matching dependency names for this configuration.
     */
    static const char *const config_b_dep[] = {"config-a"};
    entries[1].dependencies = config_b_dep;

    qm_service_manager_init();

    assert(qm_service_config_apply(
        entries, 2) == QM_SERVICE_CONFIG_OK);

    assert(qm_service_count() == 2);

    assert(qm_service_get_state(
        "config-a", &state) == QM_SERVICE_OK);
    assert(state == QM_SERVICE_REGISTERED);

    assert(qm_service_config_apply(
        entries, 2) == QM_SERVICE_CONFIG_ERR_STATE);
}

int main(void)
{
    test_valid_configuration();
    test_missing_dependency();
    test_cycle_detection();
    test_duplicate_entries();
    test_duplicate_dependencies();
    test_invalid_entries();
    test_apply();

    puts("[PASS] qm_service_config tests");
    return 0;
}
