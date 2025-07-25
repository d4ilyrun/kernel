#ifndef UAPI_LIMITS_H
#define UAPI_LIMITS_H

#include <limits.h>

#define _XOPEN_IOV_MAX 16
#define _XOPEN_NAME_MAX 255

#define IOV_MAX _XOPEN_IOV_MAX
#define NAME_MAX _XOPEN_NAME_MAX

#endif /* UAPI_LIMITS_H */
