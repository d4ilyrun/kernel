#include <dailyrun/arch/i686/syscalls.h>

#include <stdarg.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include <sys/socket.h>

char **environ; /* pointer to array of char * strings that define the current
                   environment variables */

#define alias(f)      __attribute__((__alias__(f)))
#define weak_alias(f) __attribute__((__weak__)) alias(f)

/*
 * Regular syscalls
 */

#define DEFINE_SYSCALL_ALIAS_default(_ret_type, _syscall) \
    _ret_type _syscall() weak_alias("_"#_syscall);

#define DEFINE_SYSCALL_0_default(_ret_type, _syscall, _nr, ...)          \
    _ret_type _##_syscall(void)                                          \
    {                                                                    \
        _ret_type ret;                                                   \
        __asm__ volatile("int $0x80" : "=a"(ret) : "a"(_nr) : "memory"); \
        if (ret < 0) {                                                   \
            errno = -(int)ret;                                           \
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
            errno = -(int)ret;                                          \
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
            errno = -(int)ret;                                                 \
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
            errno = -(int)ret;                                             \
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
            errno = -(int)ret;                                                \
            ret = (_ret_type) - 1;                                            \
        }                                                                     \
        return ret;                                                           \
    }

#define DEFINE_SYSCALL_5_default(_ret_type, _syscall, _nr, _type1, _type2,    \
                                 _type3, _type4, _type5, ...)                 \
    _ret_type _##_syscall(_type1 arg1, _type2 arg2, _type3 arg3, _type4 arg4, \
                          _type5 arg5, ##__VA_ARGS__)                         \
    {                                                                         \
        _ret_type ret;                                                        \
        __asm__ volatile("int $0x80"                                          \
                         : "=a"(ret)                                          \
                         : "a"(_nr), "b"(arg1), "c"(arg2), "d"(arg3),         \
                           "S"(arg4), "D"(arg5)                               \
                         : "memory");                                         \
        if (ret < 0) {                                                        \
            errno = -(int)ret;                                                \
            ret = (_ret_type) - 1;                                            \
        }                                                                     \
        return ret;                                                           \
    }

#define DEFINE_SYSCALL_6_default(_ret_type, _syscall, _nr, _type1, _type2,    \
                                 _type3, _type4, _type5, _type6, ...)         \
    _ret_type _##_syscall(_type1 arg1, _type2 arg2, _type3 arg3, _type4 arg4, \
                          _type5 arg5, _type6 arg6, ##__VA_ARGS__)            \
    {                                                                         \
        _ret_type ret;                                                        \
                                                                              \
        __asm__ volatile("push %%ebp      \n\t"                               \
                         "mov  %[a6], %%ebp \n\t"                             \
                         "int  $0x80      \n\t"                               \
                         "pop  %%ebp"                                         \
                         : "=a"(ret)                                          \
                         : "a"(_nr), "b"(arg1), "c"(arg2), "d"(arg3),         \
                           "S"(arg4), "D"(arg5), [a6] "rm"(arg6)              \
                         : "memory");                                         \
        if (ret < 0) {                                                        \
            errno = -(int)ret;                                                \
            ret = (_ret_type) - 1;                                            \
        }                                                                     \
        return ret;                                                           \
    }

/*
 * Noreturn syscalls (exit)
 */

#define DEFINE_SYSCALL_ALIAS_noreturn(_ret_type, _syscall) \
    _ret_type _syscall() weak_alias("_"#_syscall);

#define DEFINE_SYSCALL_1_noreturn(_ret_type, _syscall, _nr, ...) \
    _ret_type _##_syscall(void)                                  \
    {                                                            \
        __asm__ volatile("int $0x80" :: "a"(_nr) : "memory");    \
    }

/*
 * Manual syscalls must not be generated automatically
 */

#define DEFINE_SYSCALL_ALIAS_manual(...)
#define DEFINE_SYSCALL_0_manual(...)
#define DEFINE_SYSCALL_1_manual(...)
#define DEFINE_SYSCALL_2_manual(...)
#define DEFINE_SYSCALL_3_manual(...)
#define DEFINE_SYSCALL_4_manual(...)

/*
 * Userland syscall wrappers
 */

#define DEFINE_SYSCALL(name, nr, argc, syscall_type, ret_type, ...) \
    DEFINE_SYSCALL_ALIAS_##syscall_type(ret_type, name)                            \
    DEFINE_SYSCALL_##argc##_##syscall_type(ret_type, name, nr, __VA_ARGS__)

DEFINE_SYSCALLS(DEFINE_SYSCALL)

pid_t _wait(int *status)
{
    return _waitpid(-1, status, 0);
}

/*
 * open() must be defined manually because it uses variadic args.
 */
int _open(const char *path, int oflags, ...)
{
    va_list args;
    mode_t mode;
    int ret;

    va_start(args, oflags);
    mode = va_arg(args, mode_t);
    va_end(args);

    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(SYS_open), "b"(path), "c"(oflags), "d"(mode)
                     : "memory");
    if (ret < 0) {
        errno = -ret;
        ret = -1;
    }

    return ret;
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
 *
 */
ssize_t sendto(int fd, const void *buffer, size_t length, int flags,
               const struct sockaddr *addr, socklen_t addr_len)
{
    struct iovec iov = {
        .iov_base = (void *)buffer,
        .iov_len = length,
    };
    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_name = (void *)addr,
        .msg_namelen = addr_len,
    };

    return sendmsg(fd, &msg, flags);
}

/*
 *
 */
ssize_t send(int fd, const void *buffer, size_t length, int flags)
{
    return sendto(fd, buffer, length, flags, NULL, 0);
}

/*
 *
 */
ssize_t recvfrom(int fd, void *data, size_t len,
                 int flags, struct sockaddr *addr, socklen_t *addrlen)
{
    struct iovec iov = {
        .iov_base = data,
        .iov_len = len,
    };
    struct msghdr msg = {
        .msg_name = addr,
        .msg_namelen = addrlen ? *addrlen : 0,
        .msg_iov = &iov,
        .msg_iovlen = 1,
    };
    int ret;

    ret = recvmsg(fd, &msg, flags);
    if (addrlen)
        *addrlen = msg.msg_namelen;

    return ret;
}

/*
 *
 */
ssize_t recv(int fd, void *data, size_t len, int flags)
{
    return recvfrom(fd, data, len, flags, NULL, NULL);
}
