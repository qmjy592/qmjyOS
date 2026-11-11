#include "qm_service.h"
#include "qm_ipc.h"
#include "qm_registry.h"
#include "qm_log.h"
#include "qm_cap.h"
#include "qm_service_config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define SUBJECT_ALPHA 101u
#define SUBJECT_BETA  102u

#define MESSAGE_TYPE_TEST 0x51u

static qm_ipc_endpoint_t g_alpha_endpoint;
static qm_ipc_endpoint_t g_beta_endpoint;

static int alpha_start(void)
{
    if (qm_ipc_endpoint_create(&g_alpha_endpoint) != QM_IPC_OK) {
        return -1;
    }

    if (qm_registry_register(
            "alpha", g_alpha_endpoint) != QM_REGISTRY_OK) {
        (void)qm_ipc_endpoint_destroy(g_alpha_endpoint);
        return -2;
    }

    if (qm_cap_grant(
            SUBJECT_ALPHA,
            g_alpha_endpoint,
            QM_CAP_RIGHT_READ | QM_CAP_RIGHT_WRITE) != QM_CAP_OK) {
        (void)qm_registry_unregister("alpha");
        (void)qm_ipc_endpoint_destroy(g_alpha_endpoint);
        return -3;
    }

    if (qm_log_write(
            QM_LOG_INFO, "alpha", "service started") != 0) {
        (void)qm_cap_remove(SUBJECT_ALPHA, g_alpha_endpoint);
        (void)qm_registry_unregister("alpha");
        (void)qm_ipc_endpoint_destroy(g_alpha_endpoint);
        return -4;
    }

    return 0;
}

static int alpha_stop(void)
{
    int result = 0;

    if (qm_log_write(
            QM_LOG_INFO, "alpha", "service stopping") != 0) {
        result = -1;
    }

    if (qm_cap_remove(
            SUBJECT_ALPHA, g_alpha_endpoint) != QM_CAP_OK) {
        result = -2;
    }

    if (qm_registry_unregister("alpha") != QM_REGISTRY_OK) {
        result = -3;
    }

    if (qm_ipc_endpoint_destroy(g_alpha_endpoint) != QM_IPC_OK) {
        result = -4;
    }

    return result;
}

static int beta_start(void)
{
    if (qm_ipc_endpoint_create(&g_beta_endpoint) != QM_IPC_OK) {
        return -1;
    }

    if (qm_registry_register(
            "beta", g_beta_endpoint) != QM_REGISTRY_OK) {
        (void)qm_ipc_endpoint_destroy(g_beta_endpoint);
        return -2;
    }

    if (qm_cap_grant(
            SUBJECT_BETA,
            g_beta_endpoint,
            QM_CAP_RIGHT_READ | QM_CAP_RIGHT_WRITE) != QM_CAP_OK) {
        (void)qm_registry_unregister("beta");
        (void)qm_ipc_endpoint_destroy(g_beta_endpoint);
        return -3;
    }

    if (qm_log_write(
            QM_LOG_INFO, "beta", "service started") != 0) {
        (void)qm_cap_remove(SUBJECT_BETA, g_beta_endpoint);
        (void)qm_registry_unregister("beta");
        (void)qm_ipc_endpoint_destroy(g_beta_endpoint);
        return -4;
    }

    return 0;
}

static int beta_stop(void)
{
    int result = 0;

    if (qm_log_write(
            QM_LOG_INFO, "beta", "service stopping") != 0) {
        result = -1;
    }

    if (qm_cap_remove(
            SUBJECT_BETA, g_beta_endpoint) != QM_CAP_OK) {
        result = -2;
    }

    if (qm_registry_unregister("beta") != QM_REGISTRY_OK) {
        result = -3;
    }

    if (qm_ipc_endpoint_destroy(g_beta_endpoint) != QM_IPC_OK) {
        result = -4;
    }

    return result;
}

static void test_system_integration(void)
{
    static const char *const beta_dependencies[] = {"alpha"};

    const qm_service_config_entry_t config[] = {
        {
            "alpha",
            alpha_start,
            alpha_stop,
            NULL,
            0,
            1
        },
        {
            "beta",
            beta_start,
            beta_stop,
            beta_dependencies,
            1,
            1
        }
    };

    qm_service_state_t service_state;
    qm_ipc_endpoint_t registered_endpoint = 0;
    qm_ipc_message_t received;
    qm_log_record_t log_record;
    qm_cap_rights_t rights = 0;
    const char payload[] = "hello-beta";
    size_t log_records = 0;

    qm_service_manager_init();
    qm_ipc_init();
    qm_registry_init();
    qm_log_init();
    qm_cap_init();

    g_alpha_endpoint = 0;
    g_beta_endpoint = 0;

    assert(qm_service_config_validate(
        config, 2) == QM_SERVICE_CONFIG_OK);

    assert(qm_service_config_apply(
        config, 2) == QM_SERVICE_CONFIG_OK);

    assert(qm_service_count() == 2);

    assert(qm_service_start_all() == QM_SERVICE_OK);

    assert(qm_service_get_state(
        "alpha", &service_state) == QM_SERVICE_OK);
    assert(service_state == QM_SERVICE_RUNNING);

    assert(qm_service_get_state(
        "beta", &service_state) == QM_SERVICE_OK);
    assert(service_state == QM_SERVICE_RUNNING);

    assert(g_alpha_endpoint != 0);
    assert(g_beta_endpoint != 0);
    assert(qm_ipc_endpoint_count() == 2);
    assert(qm_registry_count() == 2);

    assert(qm_registry_lookup(
        "alpha", &registered_endpoint) == QM_REGISTRY_OK);
    assert(registered_endpoint == g_alpha_endpoint);

    assert(qm_registry_lookup(
        "beta", &registered_endpoint) == QM_REGISTRY_OK);
    assert(registered_endpoint == g_beta_endpoint);

    assert(qm_cap_check(
        SUBJECT_ALPHA,
        g_alpha_endpoint,
        QM_CAP_RIGHT_READ | QM_CAP_RIGHT_WRITE) == QM_CAP_OK);

    assert(qm_cap_check(
        SUBJECT_ALPHA,
        g_alpha_endpoint,
        QM_CAP_RIGHT_CONTROL) == QM_CAP_ERR_DENIED);

    assert(qm_cap_get(
        SUBJECT_BETA, g_beta_endpoint, &rights) == QM_CAP_OK);

    assert(rights == (
        QM_CAP_RIGHT_READ | QM_CAP_RIGHT_WRITE
    ));

    assert(qm_ipc_send(
        g_alpha_endpoint,
        g_beta_endpoint,
        MESSAGE_TYPE_TEST,
        payload,
        sizeof(payload)) == QM_IPC_OK);

    assert(qm_ipc_pending(g_beta_endpoint) == 1);

    assert(qm_ipc_receive(
        g_beta_endpoint, &received) == QM_IPC_OK);

    assert(received.sender == g_alpha_endpoint);
    assert(received.type == MESSAGE_TYPE_TEST);
    assert(received.length == sizeof(payload));
    assert(memcmp(
        received.data, payload, sizeof(payload)) == 0);

    assert(qm_ipc_receive(
        g_beta_endpoint, &received) == QM_IPC_ERR_EMPTY);

    while (qm_log_read(&log_record) == 0) {
        assert(log_record.sequence != 0);
        ++log_records;
    }

    assert(log_records == 2);

    assert(qm_service_stop_all() == QM_SERVICE_OK);

    assert(qm_service_get_state(
        "alpha", &service_state) == QM_SERVICE_OK);
    assert(service_state == QM_SERVICE_STOPPED);

    assert(qm_service_get_state(
        "beta", &service_state) == QM_SERVICE_OK);
    assert(service_state == QM_SERVICE_STOPPED);

    assert(qm_ipc_endpoint_count() == 0);
    assert(qm_registry_count() == 0);
    assert(qm_cap_count() == 0);

    assert(qm_log_count() == 2);

    assert(qm_registry_lookup(
        "alpha", &registered_endpoint) ==
        QM_REGISTRY_ERR_NOT_FOUND);

    assert(qm_cap_check(
        SUBJECT_ALPHA,
        g_alpha_endpoint,
        QM_CAP_RIGHT_READ) == QM_CAP_ERR_DENIED);
}

int main(void)
{
    test_system_integration();

    puts("[PASS] qm_system_integration tests");
    return 0;
}
