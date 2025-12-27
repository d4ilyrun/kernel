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
struct user_creds {
    uid_t ruid; /*!< Real UID */
    uid_t rgid; /*!< Real GID */
    uid_t euid; /*!< Effective UID */
    uid_t egid; /*!< Effective GID */
    uid_t suid; /*!< saved UID */
    uid_t sgid; /*!< saved GID */
};

#define UID_ROOT 0

static inline bool creds_is_root(const struct user_creds *creds)
{
    return creds->ruid == UID_ROOT;
}

/** Copy the credentials from @c src into @c dst. */
void creds_copy(struct user_creds *dst, const struct user_creds *src);

#endif /* KERNEL_USER_H */

/* @} */
