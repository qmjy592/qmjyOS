#ifndef QMjy_TYPES_H
#define QMjy_TYPES_H

/*
 * qmjyOS common types
 * Architecture-independent primitive types.
 */

#include <stdint.h>

typedef uint8_t   qm_u8_t;
typedef uint16_t  qm_u16_t;
typedef uint32_t  qm_u32_t;
typedef uint64_t  qm_u64_t;

typedef int8_t    qm_s8_t;
typedef int16_t   qm_s16_t;
typedef int32_t   qm_s32_t;
typedef int64_t   qm_s64_t;

typedef __SIZE_TYPE__    qm_size_t;
typedef __PTRDIFF_TYPE__ qm_ssize_t;
typedef __UINTPTR_TYPE__ qm_uintptr_t;
typedef __INTPTR_TYPE__  qm_intptr_t;

typedef qm_u64_t qm_phys_addr_t;
typedef qm_u64_t qm_virt_addr_t;

typedef _Bool qm_bool_t;

#define QM_TRUE  ((_Bool)1)
#define QM_FALSE ((_Bool)0)

#define QM_NULL ((void *)0)

#endif
