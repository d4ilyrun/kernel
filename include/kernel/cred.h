/**
 * @file kernel/user.h
 * @brief Userland control mechanisms
 *
 * * User credentials
 *
 * @defgroup kernel/user
 * @ingroup kernel
 *
 * @{
 */

#ifndef KERNEL_USER_H
#define KERNEL_USER_H

#include <kernel/error.h>
#include <kernel/types.h>

/** User credentials. */
struct creds {
    uid_t uid;
    gid_t gid;
};

#endif /* KERNEL_USER_H */

/* @} */
