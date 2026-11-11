#ifndef QMjy_ERROR_H
#define QMjy_ERROR_H

/*
 * Generic system-wide status codes.
 * Subsystems may retain their own specialized error codes.
 */

typedef enum qm_status {
    QM_OK                  =  0,

    QM_ERR_INVALID         = -1,
    QM_ERR_NOT_FOUND       = -2,
    QM_ERR_NOT_SUPPORTED   = -3,
    QM_ERR_NO_MEMORY       = -4,
    QM_ERR_PERMISSION      = -5,
    QM_ERR_BUSY            = -6,
    QM_ERR_IO              = -7,
    QM_ERR_TIMEOUT         = -8,
    QM_ERR_EXISTS          = -9,
    QM_ERR_OVERFLOW        = -10,
    QM_ERR_ALIGNMENT       = -11,
    QM_ERR_CORRUPT         = -12,
    QM_ERR_NO_SPACE        = -13,
    QM_ERR_INTERRUPTED     = -14,
    QM_ERR_DEVICE          = -15,
    QM_ERR_STATE           = -16,
    QM_ERR_RANGE           = -17,
    QM_ERR_DEPENDENCY      = -18,
    QM_ERR_SECURITY        = -19,
    QM_ERR_UNKNOWN         = -127
} qm_status_t;

#define QM_SUCCEEDED(status) ((status) == QM_OK)
#define QM_FAILED(status)    ((status) != QM_OK)

#endif
