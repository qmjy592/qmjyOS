#ifndef QMjy_CONFIG_H
#define QMjy_CONFIG_H

/*
 * Architecture detection.
 */

#if defined(__x86_64__) || defined(_M_X64)
    #define QMjy_ARCH_X86_64 1
    #define QMjy_ARCH_NAME "x86_64"
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define QMjy_ARCH_AARCH64 1
    #define QMjy_ARCH_NAME "aarch64"
#else
    #define QMjy_ARCH_UNKNOWN 1
    #define QMjy_ARCH_NAME "unknown"
#endif

/*
 * Firmware and build configuration.
 * The build system may override these values.
 */

#ifndef QMjy_BOOT_BIOS
    #define QMjy_BOOT_BIOS 0
#endif

#ifndef QMjy_BOOT_UEFI
    #define QMjy_BOOT_UEFI 0
#endif

#ifndef QMjy_KERNEL_DEBUG
    #define QMjy_KERNEL_DEBUG 1
#endif

#ifndef QMjy_KERNEL_VERBOSE
    #define QMjy_KERNEL_VERBOSE 1
#endif

#ifndef QMjy_SMP_ENABLED
    #define QMjy_SMP_ENABLED 1
#endif

#ifndef QMjy_PREEMPTION_ENABLED
    #define QMjy_PREEMPTION_ENABLED 0
#endif

#endif
