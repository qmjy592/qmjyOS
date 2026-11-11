#ifndef QM_CAP_H
#define QM_CAP_H

#include <stddef.h>
#include <stdint.h>

#define QM_CAP_MAX_ENTRIES 128

typedef uint32_t qm_cap_subject_t;
typedef uint32_t qm_cap_object_t;
typedef uint32_t qm_cap_rights_t;

#define QM_CAP_RIGHT_READ    ((qm_cap_rights_t)1u << 0)
#define QM_CAP_RIGHT_WRITE   ((qm_cap_rights_t)1u << 1)
#define QM_CAP_RIGHT_EXEC    ((qm_cap_rights_t)1u << 2)
#define QM_CAP_RIGHT_CREATE  ((qm_cap_rights_t)1u << 3)
#define QM_CAP_RIGHT_DELETE  ((qm_cap_rights_t)1u << 4)
#define QM_CAP_RIGHT_CONTROL ((qm_cap_rights_t)1u << 5)

#define QM_CAP_RIGHT_ALL ( \
    QM_CAP_RIGHT_READ | \
    QM_CAP_RIGHT_WRITE | \
    QM_CAP_RIGHT_EXEC | \
    QM_CAP_RIGHT_CREATE | \
    QM_CAP_RIGHT_DELETE | \
    QM_CAP_RIGHT_CONTROL \
)

typedef enum {
    QM_CAP_OK = 0,
    QM_CAP_ERR_INVALID = -1,
    QM_CAP_ERR_FULL = -2,
    QM_CAP_ERR_NOT_FOUND = -3,
    QM_CAP_ERR_DENIED = -4
} qm_cap_error_t;

typedef struct {
    qm_cap_subject_t subject;
    qm_cap_object_t object;
    qm_cap_rights_t rights;
} qm_cap_entry_t;

void qm_cap_init(void);

int qm_cap_grant(
    qm_cap_subject_t subject,
    qm_cap_object_t object,
    qm_cap_rights_t rights
);

int qm_cap_revoke(
    qm_cap_subject_t subject,
    qm_cap_object_t object,
    qm_cap_rights_t rights
);

int qm_cap_check(
    qm_cap_subject_t subject,
    qm_cap_object_t object,
    qm_cap_rights_t required
);

int qm_cap_get(
    qm_cap_subject_t subject,
    qm_cap_object_t object,
    qm_cap_rights_t *rights
);

int qm_cap_remove(
    qm_cap_subject_t subject,
    qm_cap_object_t object
);

size_t qm_cap_count(void);

#endif
