#ifndef QMjy_VERSION_H
#define QMjy_VERSION_H

#define QMjy_OS_NAME          "qmjyOS"

/*
 * Development version.
 * These values describe the current project version,
 * not a claim of production readiness.
 */
#define QMjy_VERSION_MAJOR    0
#define QMjy_VERSION_MINOR    1
#define QMjy_VERSION_PATCH    0

#define QMjy_VERSION_STRING   "0.1.0"
#define QMjy_VERSION_STATUS   "development"

#define QMjy_VERSION_CODE \
    ((QMjy_VERSION_MAJOR * 10000) + \
     (QMjy_VERSION_MINOR * 100) + \
      QMjy_VERSION_PATCH)

#endif
