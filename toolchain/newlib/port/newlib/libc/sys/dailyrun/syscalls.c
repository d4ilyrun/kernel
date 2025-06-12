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
DEFINE_SYSCALL_3(int, lseek, 19, int, int, int);

/* Unimplemented syscalls */
void _exit(int val);
int close(int file);
int execve(char *name, char **argv, char **env);
int fstat(int file, struct stat *st);
int getpid();
int isatty(int file);
int kill(int pid, int sig);
int link(char *old, char *new);
int open(const char *name, int flags, ...);
int read(int file, char *ptr, int len);
caddr_t sbrk(int incr);
int stat(const char *file, struct stat *st);
clock_t times(struct tms *buf);
int unlink(char *name);
int wait(int *status);
int write(int file, char *ptr, int len);
int gettimeofday(struct timeval *p, void *z);
