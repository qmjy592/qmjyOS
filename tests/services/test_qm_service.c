#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../services/manager/qm_service.h"

static int base_started;
static int network_started;
static int app_started;

static int stop_order[8];
static int stop_count;
static int test_failures;

#define CHECK(expr) do { \
    if (!(expr)) { \
        printf("[FAIL] %s:%d: %s\n", \
               __FILE__, __LINE__, #expr); \
        ++test_failures; \
    } else { \
        printf("[PASS] %s\n", #expr); \
    } \
} while (0)

static int base_start(void)
{
    base_started = 1;
    return 0;
}

static int network_start(void)
{
    if (!base_started)
        return -10;

    network_started = 1;
    return 0;
}

static int app_start(void)
{
    if (!network_started)
        return -20;

    app_started = 1;
    return 0;
}

static int app_stop(void)
{
    if (!app_started)
        return -31;

    app_started = 0;
    stop_order[stop_count++] = 3;
    return 0;
}

static int network_stop(void)
{
    if (app_started)
        return -32;

    network_started = 0;
    stop_order[stop_count++] = 2;
    return 0;
}

static int base_stop(void)
{
    if (network_started)
        return -33;

    base_started = 0;
    stop_order[stop_count++] = 1;
    return 0;
}

static int failing_start(void)
{
    return -123;
}

static int dummy_start(void)
{
    return 0;
}

static void reset_test_state(void)
{
    base_started = 0;
    network_started = 0;
    app_started = 0;
    stop_count = 0;

    for (size_t i = 0; i < 8; ++i)
        stop_order[i] = 0;
}

static void test_dependency_lifetime_and_order(void)
{
    char dependency_name[QM_SERVICE_NAME_MAX] = "base";
    const char *network_deps[] = { dependency_name };
    const char *app_deps[] = { "network" };
    qm_service_state_t state;

    reset_test_state();
    qm_service_manager_init();

    /* Deliberately register dependents before their dependencies. */
    CHECK(qm_service_register(
        "app", app_start, app_stop,
        app_deps, 1
    ) == QM_SERVICE_OK);

    CHECK(qm_service_register(
        "network", network_start, network_stop,
        network_deps, 1
    ) == QM_SERVICE_OK);

    /* The manager must own a copy of the dependency name. */
    strcpy(dependency_name, "changed");

    CHECK(qm_service_register(
        "base", base_start, base_stop,
        NULL, 0
    ) == QM_SERVICE_OK);

    CHECK(qm_service_register(
        "base", base_start, base_stop,
        NULL, 0
    ) == QM_SERVICE_ERR_DUPLICATE);

    CHECK(qm_service_count() == 3);

    CHECK(qm_service_start("app") ==
          QM_SERVICE_ERR_DEPENDENCY);

    CHECK(qm_service_start_all() == QM_SERVICE_OK);

    CHECK(base_started == 1);
    CHECK(network_started == 1);
    CHECK(app_started == 1);

    CHECK(qm_service_stop("base") ==
          QM_SERVICE_ERR_IN_USE);

    CHECK(qm_service_stop_all() == QM_SERVICE_OK);

    CHECK(stop_count == 3);
    CHECK(stop_order[0] == 3);
    CHECK(stop_order[1] == 2);
    CHECK(stop_order[2] == 1);

    CHECK(qm_service_get_state("base", &state) ==
          QM_SERVICE_OK);

    CHECK(state == QM_SERVICE_STOPPED);
}

static void test_start_callback_error(void)
{
    int error = 0;

    qm_service_manager_init();

    CHECK(qm_service_register(
        "bad", failing_start, NULL,
        NULL, 0
    ) == QM_SERVICE_OK);

    CHECK(qm_service_start_all() ==
          QM_SERVICE_ERR_CALLBACK);

    CHECK(qm_service_get_error("bad", &error) ==
          QM_SERVICE_OK);

    CHECK(error == -123);
}

static void test_cycle_detection(void)
{
    const char *a_deps[] = { "b" };
    const char *b_deps[] = { "a" };

    qm_service_manager_init();

    CHECK(qm_service_register(
        "a", dummy_start, NULL,
        a_deps, 1
    ) == QM_SERVICE_OK);

    CHECK(qm_service_register(
        "b", dummy_start, NULL,
        b_deps, 1
    ) == QM_SERVICE_OK);

    CHECK(qm_service_start_all() ==
          QM_SERVICE_ERR_CYCLE);
}

static void test_missing_dependency(void)
{
    const char *deps[] = { "missing" };

    qm_service_manager_init();

    CHECK(qm_service_register(
        "x", dummy_start, NULL,
        deps, 1
    ) == QM_SERVICE_OK);

    CHECK(qm_service_start_all() ==
          QM_SERVICE_ERR_DEPENDENCY);
}

int main(void)
{
    test_dependency_lifetime_and_order();
    test_start_callback_error();
    test_cycle_detection();
    test_missing_dependency();

    if (test_failures) {
        printf("\n%d test(s) failed.\n", test_failures);
        return EXIT_FAILURE;
    }

    printf("\nAll service manager regression tests passed.\n");
    return EXIT_SUCCESS;
}
