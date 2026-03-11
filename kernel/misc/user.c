#include <kernel/init.h>
#include <kernel/memory/slab.h>
#include <kernel/process.h>
#include <kernel/user.h>

#include <utils/macro.h>

#define UID_NOT_SET ((uid_t) - 1)
#define GID_NOT_SET ((uid_t) - 1)

static struct kmem_cache *user_creds_cache;

/*
 *
 */
static struct user_creds *creds_alloc(void)
{
    return kmem_cache_alloc(user_creds_cache, 0);
}

/*
 * Constructor for user_creds_cache.
 */
static void creds_constructor(void *object)
{
    struct user_creds *creds = object;

    refcnt_init(&creds->ref);
}

/*
 *
 */
static void creds_free(const struct user_creds *creds)
{
    kmem_cache_free(user_creds_cache, (void *)creds);
}

/*
 * Allocate a new empty set of user credentials.
 */
struct user_creds *creds_new(void)
{
    struct user_creds *creds;

    creds = creds_alloc();
    if (!creds)
        return NULL;

    memset(creds, 0, sizeof(*creds));

    return creds;
}

/*
 *
 */
struct user_creds *creds_clone(struct user_creds *creds)
{
    struct user_creds *new_creds;

    creds_get(creds);

    new_creds = creds_alloc();
    if (!new_creds)
        goto out;

    memcpy(new_creds, creds, sizeof(struct user_creds));
    refcnt_init(&new_creds->ref);

out:
    creds_put(creds);
    return new_creds;
}


/*
 *
 */
struct user_creds *creds_get(struct user_creds *creds)
{
    refcnt_get(&creds->ref);
    return creds;
}

/*
 *
 */
void creds_put(struct user_creds *creds)
{
    if (!creds)
        return;

    if (refcnt_put(&creds->ref) == 0)
        creds_free(creds);
}

/*
 *
 */
void creds_install(struct user_creds **dest, struct user_creds *creds)
{
    WRITE_ONCE(*dest, creds);
}

/*
 *
 */
static inline bool uid_is_valid(uid_t uid)
{
    return uid != UID_NOT_SET;
}

/*
 *
 */
static inline bool gid_is_valid(gid_t gid)
{
    return gid != GID_NOT_SET;
}

/*
 *
 */
static inline bool creds_has_uid_privileges(const struct user_creds *creds)
{
    return creds_is_root(creds);
}

/*
 *
 */
static inline bool creds_has_gid_privileges(const struct user_creds *creds)
{
    return creds_is_root(creds);
}

/*
 *
 */
static int creds_set_euid(struct user_creds *dst, const struct user_creds *cur,
                          uid_t euid)
{
    if (!uid_is_valid(euid))
        return -E_INVAL;

    if (!creds_has_uid_privileges(cur) &&
        euid != cur->ruid &&
        euid != cur->euid &&
        euid != cur->suid)
        return -E_PERM;

    dst->euid = euid;

    return -E_SUCCESS;
}

/*
 *
 */
static int creds_set_egid(struct user_creds *dst, gid_t egid)
{
    if (!gid_is_valid(egid))
        return -E_INVAL;

    if (!creds_has_gid_privileges(dst) &&
        egid != dst->rgid &&
        egid != dst->egid &&
        egid != dst->sgid)
        return -E_PERM;

    dst->egid = egid;

    return -E_SUCCESS;
}

/*
 *
 */
uid_t sys_getuid(void)
{
    return current->process->creds->ruid;
}

/*
 *
 */
uid_t sys_geteuid(void)
{
    return current->process->creds->euid;
}

/*
 *
 */
int sys_setuid(uid_t uid)
{
    struct process *process = current->process;
    struct user_creds *creds;
    error_t ret = E_SUCCESS;

    if (!uid_is_valid(uid))
        return -E_INVAL;

    creds = creds_clone(process->creds);

    if (!creds_has_uid_privileges(creds)) {
        ret = creds_set_euid(creds, creds, uid);
    } else {
        creds->ruid = uid;
        creds->euid = uid;
        creds->suid = uid;
    }

    if (ret == E_SUCCESS)
        creds_install(&process->creds, creds);
    else
        creds_put(creds);

    return ret;
}

/*
 *
 */
int sys_seteuid(uid_t euid)
{
    struct process *process = current->process;
    struct user_creds *creds;
    error_t ret = E_SUCCESS;

    creds = creds_clone(process->creds);

    ret = creds_set_euid(creds, creds, euid);
    if (ret) {
        creds_put(creds);
        return ret;
    }

    creds_install(&process->creds, creds);

    return E_SUCCESS;
}

/*
 *
 */
int sys_setreuid(uid_t ruid, uid_t euid)
{
    struct process *process = current->process;
    struct user_creds *old_creds;
    struct user_creds *new_creds;
    error_t ret = E_SUCCESS;

    if (ruid != UID_NOT_SET && !uid_is_valid(ruid))
        return -E_INVAL;

    if (euid != UID_NOT_SET && !uid_is_valid(euid))
        return -E_INVAL;

    old_creds = creds_get(process->creds);
    new_creds = creds_clone(old_creds);

    if (ruid != UID_NOT_SET) {
        if (!creds_has_uid_privileges(new_creds) &&
            ruid != new_creds->ruid &&
            ruid != new_creds->euid) {
            ret = -E_PERM;
            goto out;
        }
        new_creds->ruid = ruid;
    }

    if (euid != UID_NOT_SET) {
        ret = creds_set_euid(new_creds, old_creds, euid);
        if (ret)
            goto out;
    }

    if ((ruid != UID_NOT_SET) ||
        (euid != UID_NOT_SET && euid != old_creds->ruid))
        new_creds->suid = new_creds->euid;

    creds_install(&process->creds, new_creds);

out:
    if (ret != E_SUCCESS)
        creds_put(new_creds);
    creds_put(old_creds);
    return ret;
}

/*
 *
 */
int sys_setresuid(uid_t ruid, uid_t euid, uid_t suid)
{
    struct process *process = current->process;
    struct user_creds *old_creds;
    struct user_creds *new_creds;
    error_t ret;

#define __setresuid(_uid)                                             \
    do {                                                              \
        ret = -E_INVAL;                                               \
        if (_uid != UID_NOT_SET && !uid_is_valid(_uid))               \
            goto out;                                                 \
                                                                      \
        ret = -E_PERM;                                                \
        if (_uid != UID_NOT_SET) {                                    \
            if (!creds_has_uid_privileges(old_creds) &&               \
                _uid != old_creds->ruid &&                            \
                _uid != old_creds->suid &&                            \
                _uid != old_creds->euid)                              \
                goto out;                                             \
            new_creds->_uid = _uid;                                   \
        }                                                             \
    } while (0)

    old_creds = creds_get(process->creds);
    new_creds = creds_clone(old_creds);

    __setresuid(ruid);
    __setresuid(euid);
    __setresuid(suid);

    ret = E_SUCCESS;
    creds_install(&process->creds, new_creds);

out:
    if (ret != E_SUCCESS)
        creds_put(new_creds);
    creds_put(old_creds);
    return ret;
}

/*
 *
 */
gid_t sys_getgid(void)
{
    return current->process->creds->rgid;
}

/*
 *
 */
gid_t sys_getegid(void)
{
    return current->process->creds->egid;
}

/*
 *
 */
int sys_setgid(gid_t gid)
{
    struct process *process = current->process;
    struct user_creds *creds;
    error_t ret = E_SUCCESS;

    if (!gid_is_valid(gid))
        return -E_INVAL;

    creds = creds_clone(process->creds);

    if (!creds_has_gid_privileges(creds)) {
        ret = creds_set_egid(creds, gid);
    } else {
        creds->rgid = gid;
        creds->egid = gid;
        creds->sgid = gid;
    }

    if (ret == E_SUCCESS)
        creds_install(&process->creds, creds);
    else
        creds_put(creds);

    return ret;
}

/*
 *
 */
int sys_setegid(gid_t egid)
{
    struct process *process = current->process;
    struct user_creds *creds;
    error_t ret = E_SUCCESS;

    creds = creds_clone(process->creds);

    ret = creds_set_egid(creds, egid);
    if (ret) {
        creds_put(creds);
        return ret;
    }

    creds_install(&process->creds, creds);

    return ret;
}

/*
 *
 */
int sys_setregid(gid_t rgid, gid_t egid)
{
    struct process *process = current->process;
    struct user_creds *old_creds;
    struct user_creds *new_creds;
    error_t ret = E_SUCCESS;

    if (rgid != GID_NOT_SET && !gid_is_valid(rgid))
        return -E_INVAL;

    if (egid != GID_NOT_SET && !gid_is_valid(egid))
        return -E_INVAL;

    old_creds = creds_get(process->creds);
    new_creds = creds_clone(old_creds);

    ret = -E_PERM;
    if (rgid != GID_NOT_SET) {
        if (!creds_has_gid_privileges(old_creds) &&
            rgid != old_creds->rgid &&
            rgid != old_creds->egid)
            goto out;
        new_creds->rgid = rgid;
    }

    ret = -E_PERM;
    if (egid != GID_NOT_SET) {
        ret = creds_set_egid(new_creds, egid);
        if (ret)
            goto out;
    }

    if ((rgid != GID_NOT_SET) ||
        (egid != GID_NOT_SET && egid != old_creds->rgid))
        new_creds->sgid = new_creds->egid;

    ret = E_SUCCESS;
    creds_install(&process->creds, new_creds);

out:
    if (ret)
        creds_put(new_creds);
    creds_put(old_creds);
    return ret;
}

/*
 * FIXME: prevent reads during update operations (also in the other functions)
 */
int sys_setresgid(gid_t rgid, gid_t egid, gid_t sgid)
{
    struct process *process = current->process;
    struct user_creds *old_creds;
    struct user_creds *new_creds;
    error_t ret;

    old_creds = creds_get(process->creds);
    new_creds = creds_clone(old_creds);

#define __setresgid(_gid)                                             \
    do {                                                              \
        ret = -E_INVAL;                                               \
        if (_gid != GID_NOT_SET && !gid_is_valid(_gid))               \
            goto out;                                                 \
                                                                      \
        ret = -E_PERM;                                                \
        if (_gid != GID_NOT_SET) {                                    \
            if (!creds_has_gid_privileges(old_creds) &&               \
                _gid != old_creds->rgid &&                            \
                _gid != old_creds->sgid &&                            \
                _gid != old_creds->egid)                              \
                goto out;                                             \
            new_creds->_gid = _gid;                                   \
        }                                                             \
    } while (0)

    __setresgid(rgid);
    __setresgid(egid);
    __setresgid(sgid);

    ret = E_SUCCESS;
    creds_install(&process->creds, new_creds);

out:
    if (ret != E_SUCCESS)
        creds_put(new_creds);
    creds_put(old_creds);
    return ret;
}

/*
 *
 */
static error_t user_api_init(void)
{
	user_creds_cache = kmem_cache_create("user_creds",
					     sizeof(struct user_creds), CPU_CACHE_ALIGN,
					     creds_constructor, NULL);
	if (IS_ERR(user_creds_cache))
		return ERR_FROM_PTR(user_creds_cache);

	return E_SUCCESS;
}

DECLARE_INITCALL(INIT_NORMAL, user_api_init);
