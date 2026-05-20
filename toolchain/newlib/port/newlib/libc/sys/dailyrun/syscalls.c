#include <dailyrun/arch/i686/syscalls.h>

#include <stdint.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>

char **environ; /* pointer to array of char * strings that define the current
                   environment variables */

/*
 * Regular syscalls
 */

#define DEFINE_SYSCALL_0_default(_ret_type, _syscall, _nr, ...)          \
    _ret_type _##_syscall(void)                                          \
    {                                                                    \
        _ret_type ret;                                                   \
        __asm__ volatile("int $0x80" : "=a"(ret) : "a"(_nr) : "memory"); \
        if (ret < 0) {                                                   \
            errno = (int)ret;                                            \
            ret = (_ret_type) - 1;                                       \
        }                                                                \
        return ret;                                                      \
    }

#define DEFINE_SYSCALL_1_default(_ret_type, _syscall, _nr, _type1, ...) \
    _ret_type _##_syscall(_type1 arg1 __VA_OPT__(, ) __VA_ARGS__)       \
    {                                                                   \
        _ret_type ret;                                                  \
        __asm__ volatile("int $0x80"                                    \
                         : "=a"(ret)                                    \
                         : "a"(_nr), "b"(arg1)                          \
                         : "memory");                                   \
        if (ret < 0) {                                                  \
            errno = (int)ret;                                           \
            ret = (_ret_type) - 1;                                      \
        }                                                               \
        return ret;                                                     \
    }

#define DEFINE_SYSCALL_2_default(_ret_type, _syscall, _nr, _type1, _type2,     \
                                 ...)                                          \
    _ret_type _##_syscall(_type1 arg1, _type2 arg2 __VA_OPT__(, ) __VA_ARGS__) \
    {                                                                          \
        _ret_type ret;                                                         \
        __asm__ volatile("int $0x80"                                           \
                         : "=a"(ret)                                           \
                         : "a"(_nr), "b"(arg1), "c"(arg2)                      \
                         : "memory");                                          \
        if (ret < 0) {                                                         \
            errno = (int)ret;                                                  \
            ret = (_ret_type) - 1;                                             \
        }                                                                      \
        return ret;                                                            \
    }

#define DEFINE_SYSCALL_3_default(_ret_type, _syscall, _nr, _type1, _type2, \
                                 _type3, ...)                              \
    _ret_type _##_syscall(_type1 arg1, _type2 arg2,                        \
                          _type3 arg3 __VA_OPT__(, ) __VA_ARGS__)          \
    {                                                                      \
        _ret_type ret;                                                     \
        __asm__ volatile("int $0x80"                                       \
                         : "=a"(ret)                                       \
                         : "a"(_nr), "b"(arg1), "c"(arg2), "d"(arg3)       \
                         : "memory");                                      \
        if (ret < 0) {                                                     \
            errno = (int)ret;                                              \
            ret = (_ret_type) - 1;                                         \
        }                                                                  \
        return ret;                                                        \
    }

#define DEFINE_SYSCALL_4_default(_ret_type, _syscall, _nr, _type1, _type2,    \
                                 _type3, _type4, ...)                         \
    _ret_type _##_syscall(_type1 arg1, _type2 arg2, _type3 arg3, _type4 arg4, \
                          ##__VA_ARGS__)                                      \
    {                                                                         \
        _ret_type ret;                                                        \
        __asm__ volatile("int $0x80"                                          \
                         : "=a"(ret)                                          \
                         : "a"(_nr), "b"(arg1), "c"(arg2), "d"(arg3),         \
                           "S"(arg4)                                          \
                         : "memory");                                         \
        if (ret < 0) {                                                        \
            errno = (int)ret;                                                 \
            ret = (_ret_type) - 1;                                            \
        }                                                                     \
        return ret;                                                           \
    }

/*
 * Noreturn syscalls (exit)
 */

#define DEFINE_SYSCALL_1_noreturn(_ret_type, _syscall, _nr, ...) \
    _ret_type _##_syscall(void)                                  \
    {                                                            \
        __asm__ volatile("int $0x80" :: "a"(_nr) : "memory");    \
    }

/*
 * Userland syscall wrappers
 */

#define DEFINE_SYSCALL(name, nr, argc, syscall_type, ret_type, ...) \
    DEFINE_SYSCALL_##argc##_##syscall_type(ret_type, name, nr, __VA_ARGS__)

DEFINE_SYSCALLS(DEFINE_SYSCALL)

/*
 * Syscalls that are wrapper around other syscalls.
 */

pid_t _wait(int *status)
{
    return _waitpid(-1, status, 0);
}

/*
 * signal() should not be used for anything else than setting the handler
 * to SIGDFL or SIGIGN. For other values follow the BSD semantics: the signal
 * is masked until delivered and handler (also the same as glibc).
 */
sig_sa_handler_t signal(int signo, sig_sa_handler_t handler)
{
    struct sigaction sigaction = {
        .sa_handler = handler,
        .sa_flags = 0,
    };

    sigemptyset(&sigaction.sa_mask);
    sigaddset(&sigaction.sa_mask, signo);

    if (_sigaction(signo, &sigaction, &sigaction) < 0)
        return SIG_ERR;

    return sigaction.sa_handler;
}

/*
 * Simple syscall wrappers that are not defined by newlib.
 */

#define alias(f) __attribute__((__alias__(f)))
#define weak_alias(f) __attribute__((__alias__(f))) __attribute__((__weak__))

#define stringify(x) __stringify(x)
#define __stringify(x) #x

#define DEFINE_SYSCALL_ALIAS(ret_type, sc, ...) \
    ret_type sc(__VA_ARGS__) weak_alias(stringify(_##sc))


DEFINE_SYSCALL_ALIAS(int, lstat, const char *, struct stat *);

DEFINE_SYSCALL_ALIAS(uid_t, getuid, void);
DEFINE_SYSCALL_ALIAS(uid_t, geteuid);
DEFINE_SYSCALL_ALIAS(gid_t, getgid, void);
DEFINE_SYSCALL_ALIAS(gid_t, getegid);
DEFINE_SYSCALL_ALIAS(int,   waitpid, pid_t, int *, int);

DEFINE_SYSCALL_ALIAS(int, sigprocmask, int, const sigset_t *, sigset_t *);
DEFINE_SYSCALL_ALIAS(int, sigaction, int, const struct sigaction *, struct sigaction *);
DEFINE_SYSCALL_ALIAS(int, sigreturn, ucontext_t *);
DEFINE_SYSCALL_ALIAS(int, sigsethandler, sig_sa_sigaction_t);
