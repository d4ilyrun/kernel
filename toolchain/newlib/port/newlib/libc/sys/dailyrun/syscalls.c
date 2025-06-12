#include <stdio.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>

/* TODO: Expose syscall numbers through uapi headers */

char **environ; /* pointer to array of char * strings that define the current
                   environment variables */

#define DEFINE_SYSCALL_0(_ret_type, _syscall, _nr)                       \
    _ret_type _##_syscall(void)                                          \
    {                                                                    \
        int ret = _nr;                                                   \
        __asm__ volatile("int $0x80" : "=a"(ret) : "a"(ret) : "memory"); \
        if (ret < 0) {                                                   \
            errno = ret;                                                 \
            ret = -1;                                                    \
        }                                                                \
        return ret;                                                      \
    }

#define DEFINE_SYSCALL_2(_ret_type, _syscall, _nr, _type1, _type2) \
    _ret_type _##_syscall(_type1 arg1, _type2 arg2)                \
    {                                                              \
        int ret = _nr;                                             \
        __asm__ volatile("int $0x80"                               \
                         : "=a"(ret)                               \
                         : "a"(ret), "b"(arg1), "c"(arg2)          \
                         : "memory");                              \
        if (ret < 0) {                                             \
            errno = ret;                                           \
            ret = -1;                                              \
        }                                                          \
        return ret;                                                \
    }

#define DEFINE_SYSCALL_3(_ret_type, _syscall, _nr, _type1, _type2, _type3) \
    _ret_type _##_syscall(_type1 arg1, _type2 arg2, _type3 arg3)           \
    {                                                                      \
        int ret = _nr;                                                     \
        __asm__ volatile("int $0x80"                                       \
                         : "=a"(ret)                                       \
                         : "a"(ret), "b"(arg1), "c"(arg2), "d"(arg3)       \
                         : "memory");                                      \
        if (ret < 0) {                                                     \
            errno = ret;                                                   \
            ret = -1;                                                      \
        }                                                                  \
        return ret;                                                        \
    }

DEFINE_SYSCALL_0(int, fork, 2);
DEFINE_SYSCALL_3(int, read, 3, int, char *, int);
DEFINE_SYSCALL_3(int, lseek, 19, int, int, int);

/*
 * Open cannot be declared using the regular macros because it takes in
 * a variadic parameter.
 */
int _open(const char *path, int oflags, ...)
{
    int eax = 5;

    __asm__ volatile("int $0x80"
                     : "=a"(eax)
                     : "a"(eax), "b"(path), "c"(oflags)
                     : "memory");

    if (eax < 0) {
        errno = eax;
        eax = -1;
    }

    return eax;
}

/* Unimplemented syscalls */
void _exit(int val);
int close(int file);
int execve(char *name, char **argv, char **env);
int fstat(int file, struct stat *st);
int getpid();
int isatty(int file);
int kill(int pid, int sig);
int link(char *old, char *new);
caddr_t sbrk(int incr);
int stat(const char *file, struct stat *st);
clock_t times(struct tms *buf);
int unlink(char *name);
int wait(int *status);
int write(int file, char *ptr, int len);
int gettimeofday(struct timeval *p, void *z);
