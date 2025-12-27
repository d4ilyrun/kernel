#include <kernel/process.h>
#include <kernel/user.h>

#include <utils/macro.h>

#define UID_NOT_SET ((uid_t) - 1)
#define GID_NOT_SET ((uid_t) - 1)

/*
 *
 */
void creds_copy(struct user_creds *dst, const struct user_creds *src)
{
    memcpy(dst, src, sizeof(struct user_creds));
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
    struct process *process = current->process;
    uid_t uid;

    locked_scope (&process->lock) {
        uid = process->creds.ruid;
    }

    return uid;
}

/*
 *
 */
uid_t sys_geteuid(void)
{
    struct process *process = current->process;
    uid_t uid;

    locked_scope (&process->lock) {
        uid = process->creds.euid;
    }

    return uid;
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

    locked_scope (&process->lock) {
        creds = &process->creds;

        if (!creds_has_uid_privileges(creds)) {
            ret = creds_set_euid(creds, creds, uid);
        } else {
            creds->ruid = uid;
            creds->euid = uid;
            creds->suid = uid;
        }
    }

    return ret;
}

/*
 *
 */
int sys_seteuid(uid_t euid)
{
    struct process *process = current->process;
    error_t ret = E_SUCCESS;

    locked_scope (&process->lock) {
        ret = creds_set_euid(&process->creds, &process->creds, euid);
    }

    return ret;
}

/*
 *
 */
int sys_setreuid(uid_t ruid, uid_t euid)
{
    struct process *process = current->process;
    struct user_creds creds;
    error_t ret;

    if (ruid != UID_NOT_SET && !uid_is_valid(ruid))
        return -E_INVAL;

    if (euid != UID_NOT_SET && !uid_is_valid(euid))
        return -E_INVAL;

    locked_scope (&process->lock) {
        creds_copy(&creds, &process->creds);

        if (ruid != UID_NOT_SET) {
            if (!creds_has_uid_privileges(&creds) &&
                ruid != creds.ruid &&
                ruid != creds.euid)
                return -E_PERM;
            creds.ruid = ruid;
        }

        if (euid != UID_NOT_SET) {
            ret = creds_set_euid(&creds, &process->creds, euid);
            if (ret)
                return ret;
        }

        if ((ruid != UID_NOT_SET) ||
            (euid != UID_NOT_SET && euid != process->creds.ruid))
            creds.suid = creds.euid;

        creds_copy(&process->creds, &creds);
    }

    return -E_SUCCESS;
}

/*
 *
 */
int sys_setresuid(uid_t ruid, uid_t euid, uid_t suid)
{
    struct process *process = current->process;
    struct user_creds *old_creds;
    struct user_creds new_creds;

    locked_scope (&process->lock) {

        creds_copy(&new_creds, old_creds);

#define __setresuid(_uid)                                               \
    do {                                                                \
        if (_uid != UID_NOT_SET && !uid_is_valid(_uid))                 \
            return -E_INVAL;                                             \
                                                                        \
        if (_uid != UID_NOT_SET) {                                      \
            if (!creds_has_uid_privileges(old_creds) &&                 \
                _uid != old_creds->ruid &&                              \
                _uid != old_creds->suid &&                              \
                _uid != old_creds->euid)                                \
                return -E_PERM;                                          \
            new_creds._uid = _uid;                                      \
        }                                                               \
    } while (0)

        old_creds = &process->creds;
        __setresuid(ruid);
        __setresuid(euid);
        __setresuid(suid);

#undef __setresuid

        creds_copy(&process->creds, &new_creds);
    }

    return -E_SUCCESS;
}

/*
 *
 */
gid_t sys_getgid(void)
{
    struct process *process = current->process;
    gid_t gid;

    locked_scope (&process->lock) {
        gid = process->creds.rgid;
    }

    return gid;
}

/*
 *
 */
gid_t sys_getegid(void)
{
    struct process *process = current->process;
    gid_t gid;

    locked_scope (&process->lock) {
        gid = process->creds.egid;
    }

    return gid;
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

    locked_scope (&process->lock) {
        creds = &process->creds;

        if (!creds_has_gid_privileges(creds)) {
            ret = creds_set_egid(&process->creds, gid);
        } else {
            creds->rgid = gid;
            creds->egid = gid;
            creds->sgid = gid;
        }
    }

    return ret;
}

/*
 *
 */
int sys_setegid(gid_t egid)
{
    struct process *process = current->process;
    error_t ret = E_SUCCESS;

    locked_scope (&process->lock) {
        ret = creds_set_egid(&process->creds, egid);
    }

    return ret;
}

/*
 *
 */
int sys_setregid(gid_t rgid, gid_t egid)
{
    struct process *process = current->process;
    struct user_creds creds;
    error_t ret;

    if (rgid != GID_NOT_SET && !gid_is_valid(rgid))
        return -E_INVAL;

    if (egid != GID_NOT_SET && !gid_is_valid(egid))
        return -E_INVAL;

    locked_scope (&process->lock) {
        creds_copy(&creds, &process->creds);

        if (rgid != GID_NOT_SET) {
            if (!creds_has_gid_privileges(&creds) &&
                rgid != creds.rgid &&
                rgid != creds.egid)
                return -E_PERM;
            creds.rgid = rgid;
        }

        if (egid != GID_NOT_SET) {
            ret = creds_set_egid(&creds, egid);
            if (ret)
                return ret;
        }

        if ((rgid != GID_NOT_SET) ||
            (egid != GID_NOT_SET && egid != process->creds.rgid))
            creds.sgid = creds.egid;

        creds_copy(&process->creds, &creds);
    }

    return -E_SUCCESS;
}

/*
 *
 */
int sys_setresgid(gid_t rgid, gid_t egid, gid_t sgid)
{
    struct process *process = current->process;
    struct user_creds *old_creds;
    struct user_creds new_creds;

    locked_scope (&process->lock) {

        creds_copy(&new_creds, old_creds);

#define __setresgid(_gid)                                               \
    do {                                                                \
        if (_gid != GID_NOT_SET && !gid_is_valid(_gid))                 \
            return -E_INVAL;                                             \
                                                                        \
        if (_gid != GID_NOT_SET) {                                      \
            if (!creds_has_gid_privileges(old_creds) &&                 \
                _gid != old_creds->rgid &&                              \
                _gid != old_creds->sgid &&                              \
                _gid != old_creds->egid)                                \
                return -E_PERM;                                          \
            new_creds._gid = _gid;                                      \
        }                                                               \
    } while (0)

        old_creds = &process->creds;
        __setresgid(rgid);
        __setresgid(egid);
        __setresgid(sgid);

#undef __setresgid

        creds_copy(&process->creds, &new_creds);
    }

    return -E_SUCCESS;
}
