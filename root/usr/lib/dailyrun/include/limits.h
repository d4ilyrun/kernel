#ifndef _LIMITS_H
#define _LIMITS_H

#define _XOPEN_IOV_MAX 16
#define _XOPEN_NAME_MAX 255

#define IOV_MAX _XOPEN_IOV_MAX
#define NAME_MAX _XOPEN_NAME_MAX

#include_next <limits.h>

#endif /* _LIMITS_H */
