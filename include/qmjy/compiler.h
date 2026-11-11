#ifndef QMjy_COMPILER_H
#define QMjy_COMPILER_H

/*
 * Compiler abstraction layer.
 */

#if defined(__clang__)
    #define QMjy_COMPILER_CLANG 1
#elif defined(__GNUC__)
    #define QMjy_COMPILER_GCC 1
#else
    #define QMjy_COMPILER_UNKNOWN 1
#endif

#if defined(__GNUC__) || defined(__clang__)

    #define QMjy_PACKED          __attribute__((packed))
    #define QMjy_ALIGNED(n)      __attribute__((aligned(n)))
    #define QMjy_NORETURN        __attribute__((noreturn))
    #define QMjy_UNUSED          __attribute__((unused))
    #define QMjy_ALWAYS_INLINE   inline __attribute__((always_inline))
    #define QMjy_WEAK            __attribute__((weak))
    #define QMjy_SECTION(name)   __attribute__((section(name)))

#else

    #define QMjy_PACKED
    #define QMjy_ALIGNED(n)
    #define QMjy_NORETURN
    #define QMjy_UNUSED
    #define QMjy_ALWAYS_INLINE inline
    #define QMjy_WEAK
    #define QMjy_SECTION(name)

#endif

#define QMjy_COMPILER_BARRIER() \
    __asm__ __volatile__("" ::: "memory")

#define QMjy_MEMORY_BARRIER() \
    __atomic_thread_fence(__ATOMIC_SEQ_CST)

#define QMjy_READ_ONCE(x) \
    __atomic_load_n(&(x), __ATOMIC_RELAXED)

#define QMjy_WRITE_ONCE(x, value) \
    __atomic_store_n(&(x), (value), __ATOMIC_RELAXED)

#endif
