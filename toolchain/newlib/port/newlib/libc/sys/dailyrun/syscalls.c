#include <uapi/arch/i686/syscalls.h>

#include <stdint.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
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
 * Unimplemented syscalls
 */

int link(char *old, char *new);
clock_t times(struct tms *buf);
int unlink(char *name);
int gettimeofday(struct timeval *p, void *z);
