#include <stdio.h>
#include <stdlib.h>

#include "../../services/manager/qm_service.h"

static int base_started;
static int network_started;
static int app_started;

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

int main(void)
{
    const char *network_deps[] = { "base" };
    const char *app_deps[] = { "network" };
    qm_service_state_t state;

    qm_service_manager_init();

    CHECK(qm_service_register(
        "app", app_start, NULL,
        app_deps, 1
    ) == QM_SERVICE_OK);

    CHECK(qm_service_register(
        "network", network_start, NULL,
        network_deps, 1
    ) == QM_SERVICE_OK);

    CHECK(qm_service_register(
        "base", base_start, NULL,
        NULL, 0
    ) == QM_SERVICE_OK);

    CHECK(qm_service_register(
        "base", base_start, NULL,
        NULL, 0
    ) == QM_SERVICE_ERR_DUPLICATE);

    CHECK(qm_service_count() == 3);

    CHECK(qm_service_start("app") ==
          QM_SERVICE_ERR_DEPENDENCY);

    CHECK(qm_service_start_all() == QM_SERVICE_OK);

    CHECK(base_started == 1);
    CHECK(network_started == 1);
    CHECK(app_started == 1);

    CHECK(qm_service_get_state("app", &state) ==
          QM_SERVICE_OK);

    CHECK(state == QM_SERVICE_RUNNING);

    CHECK(qm_service_stop("app") == QM_SERVICE_OK);

    CHECK(qm_service_get_state("app", &state) ==
          QM_SERVICE_OK);

    CHECK(state == QM_SERVICE_STOPPED);

    if (test_failures) {
        printf("\n%d test(s) failed.\n", test_failures);
        return EXIT_FAILURE;
    }

    printf("\nAll service manager tests passed.\n");
    return EXIT_SUCCESS;
}
