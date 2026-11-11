#ifndef QMJY_DEVICE_H
#define QMJY_DEVICE_H

#include "types.h"
#include "error.h"

typedef qm_u64_t qm_device_id_t;

typedef enum qm_device_class {
    QM_DEVICE_UNKNOWN = 0,
    QM_DEVICE_BUS,
    QM_DEVICE_BLOCK,
    QM_DEVICE_NETWORK,
    QM_DEVICE_DISPLAY,
    QM_DEVICE_INPUT,
    QM_DEVICE_AUDIO,
    QM_DEVICE_SERIAL
} qm_device_class_t;

typedef struct qm_device {
    qm_device_id_t id;
    qm_device_class_t device_class;
    const char *name;
    const char *compatible;
    void *driver_data;
} qm_device_t;

typedef struct qm_driver {
    const char *name;
    const char *compatible;

    qm_status_t (*probe)(qm_device_t *device);
    void (*remove)(qm_device_t *device);
} qm_driver_t;

qm_status_t qm_device_register(qm_device_t *device);

qm_status_t qm_device_unregister(
    qm_device_id_t id
);

qm_status_t qm_driver_register(
    const qm_driver_t *driver
);

qm_device_t *qm_device_find(
    qm_device_id_t id
);

#endif
