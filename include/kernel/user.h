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
#include <kernel/refcnt.h>

/** User credentials. */
struct user_creds {
    refcnt_t ref;
    uid_t ruid; /*!< Real UID */
    uid_t rgid; /*!< Real GID */
    uid_t euid; /*!< Effective UID */
    uid_t egid; /*!< Effective GID */
    uid_t suid; /*!< saved UID */
    uid_t sgid; /*!< saved GID */
};

#define UID_ROOT 0
#define GID_ROOT 0

static inline bool creds_is_root(const struct user_creds *creds)
{
    return creds->ruid == UID_ROOT;
}

struct user_creds *creds_new(void);
struct user_creds *creds_get(struct user_creds *creds);
void creds_put(struct user_creds *creds);
void creds_install(struct user_creds **dest, struct user_creds *creds);
struct user_creds *creds_clone(struct user_creds *creds);

#endif /* KERNEL_USER_H */

/* @} */
