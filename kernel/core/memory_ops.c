#include <stddef.h>
#include <stdint.h>

/*
 * Freestanding kernel memory primitives.
 *
 * Volatile byte accesses prevent the compiler from recognizing these
 * implementations as loops that should be replaced by calls to themselves.
 */

void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    volatile unsigned char *vd = d;
    const volatile unsigned char *vs = s;

    for (size_t i = 0; i < n; ++i)
        vd[i] = vs[i];

    return dest;
}

void *memmove(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    uintptr_t da = (uintptr_t)d;
    uintptr_t sa = (uintptr_t)s;
    volatile unsigned char *vd = d;
    const volatile unsigned char *vs = s;

    if (da <= sa || da - sa >= n) {
        for (size_t i = 0; i < n; ++i)
            vd[i] = vs[i];
    } else {
        for (size_t i = n; i != 0; --i)
            vd[i - 1] = vs[i - 1];
    }

    return dest;
}

void *memset(void *dest, int value, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    volatile unsigned char *vd = d;
    unsigned char byte = (unsigned char)value;

    for (size_t i = 0; i < n; ++i)
        vd[i] = byte;

    return dest;
}

int memcmp(const void *lhs, const void *rhs, size_t n)
{
    const unsigned char *a = (const unsigned char *)lhs;
    const unsigned char *b = (const unsigned char *)rhs;
    const volatile unsigned char *va = a;
    const volatile unsigned char *vb = b;

    for (size_t i = 0; i < n; ++i) {
        if (va[i] != vb[i])
            return (int)va[i] - (int)vb[i];
    }

    return 0;
}
