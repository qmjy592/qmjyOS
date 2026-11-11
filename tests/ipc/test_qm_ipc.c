#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../ipc/message/qm_ipc.h"

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
    qm_ipc_endpoint_t a;
    qm_ipc_endpoint_t b;
    qm_ipc_message_t message;

    const char payload[] = "Hello qmjyOS IPC";

    qm_ipc_init();

    CHECK(qm_ipc_endpoint_create(&a) == QM_IPC_OK);
    CHECK(qm_ipc_endpoint_create(&b) == QM_IPC_OK);

    CHECK(a != b);
    CHECK(qm_ipc_endpoint_count() == 2);

    CHECK(qm_ipc_receive(b, &message) == QM_IPC_ERR_EMPTY);

    CHECK(qm_ipc_send(
        a, b, 100,
        payload, sizeof(payload)
    ) == QM_IPC_OK);

    CHECK(qm_ipc_pending(b) == 1);

    CHECK(qm_ipc_receive(b, &message) == QM_IPC_OK);

    CHECK(message.sender == a);
    CHECK(message.type == 100);
    CHECK(message.length == sizeof(payload));

    CHECK(memcmp(
        message.data,
        payload,
        sizeof(payload)
    ) == 0);

    CHECK(qm_ipc_pending(b) == 0);

    CHECK(qm_ipc_send(
        a, b, 1,
        payload, QM_IPC_DATA_SIZE + 1
    ) == QM_IPC_ERR_TOO_LARGE);

    CHECK(qm_ipc_endpoint_destroy(a) == QM_IPC_OK);

    CHECK(qm_ipc_send(
        a, b, 1,
        payload, sizeof(payload)
    ) == QM_IPC_ERR_NOT_FOUND);

    CHECK(qm_ipc_endpoint_destroy(b) == QM_IPC_OK);
    CHECK(qm_ipc_endpoint_count() == 0);

    if (failures) {
        printf("\n%d test(s) failed.\n", failures);
        return EXIT_FAILURE;
    }

    printf("\nAll IPC tests passed.\n");
    return EXIT_SUCCESS;
}
