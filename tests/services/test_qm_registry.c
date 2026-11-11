#include <stdio.h>
#include <stdlib.h>

#include "../../services/registry/qm_registry.h"

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
    qm_registry_endpoint_t endpoint = 0;

    qm_registry_init();

    CHECK(qm_registry_register("logger", 10) == QM_REGISTRY_OK);
    CHECK(qm_registry_register("network", 20) == QM_REGISTRY_OK);

    CHECK(qm_registry_count() == 2);

    CHECK(qm_registry_register("logger", 30) ==
          QM_REGISTRY_ERR_EXISTS);

    CHECK(qm_registry_lookup("logger", &endpoint) ==
          QM_REGISTRY_OK);
    CHECK(endpoint == 10);

    CHECK(qm_registry_lookup("missing", &endpoint) ==
          QM_REGISTRY_ERR_NOT_FOUND);

    CHECK(qm_registry_register("invalid", 0) ==
          QM_REGISTRY_ERR_INVALID);

    CHECK(qm_registry_unregister("logger") ==
          QM_REGISTRY_OK);

    CHECK(qm_registry_lookup("logger", &endpoint) ==
          QM_REGISTRY_ERR_NOT_FOUND);

    CHECK(qm_registry_count() == 1);

    if (failures) {
        printf("\n%d test(s) failed.\n", failures);
        return EXIT_FAILURE;
    }

    printf("\nAll registry tests passed.\n");
    return EXIT_SUCCESS;
}
