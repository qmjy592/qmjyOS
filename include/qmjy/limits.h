#ifndef QMjy_LIMITS_H
#define QMjy_LIMITS_H

#include "types.h"

#define QMjy_BITS_PER_BYTE       8U

#define QMjy_U8_MAX              UINT8_MAX
#define QMjy_U16_MAX             UINT16_MAX
#define QMjy_U32_MAX             UINT32_MAX
#define QMjy_U64_MAX             UINT64_MAX

#define QMjy_S8_MIN              INT8_MIN
#define QMjy_S8_MAX              INT8_MAX
#define QMjy_S16_MIN             INT16_MIN
#define QMjy_S16_MAX             INT16_MAX
#define QMjy_S32_MIN             INT32_MIN
#define QMjy_S32_MAX             INT32_MAX
#define QMjy_S64_MIN             INT64_MIN
#define QMjy_S64_MAX             INT64_MAX

#define QMjy_PAGE_SIZE           4096U
#define QMjy_PAGE_SHIFT          12U

#define QMjy_CACHE_LINE_SIZE     64U

#define QMjy_MAX_CPU_COUNT       256U
#define QMjy_MAX_PATH_LENGTH     4096U
#define QMjy_MAX_NAME_LENGTH     256U

#endif
