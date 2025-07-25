/**
 * @file kernel/syscalls.h
 *
 * @defgroup kernel_syscalls Syscalls
 * @ingroup kernel
 *
 * # Syscalls
 *
 * This module contains the definition for our kernel's syscalls.
 *
 * We'll aim to be as POSIX compliant as possible. This has multiple benefits:
 * 1. I'm familiar with these syscalls already
 * 2. It will make porting existing programs easier eventually
 * 3. I can directly re-use the knowledge I gain from implementing them
 *
 * @{
 */
#ifndef KERNEL_SYSCALLS_H
#define KERNEL_SYSCALLS_H

#if ARCH == I686
#include <kernel/arch/i686/syscalls.h>
#endif

#include <kernel/types.h>

#include <stddef.h>
#include <sys/stat.h>

typedef struct syscall_args {
    u32 nr;
    u32 arg1, arg2, arg3, arg4, arg5, arg6;
} syscall_args_t;

/* SYSCALLS HANDLER */

void sys_exit(int status);
pid_t sys_fork(void);
int sys_read(int fd, char *, size_t len);
int sys_write(int fd, const char *, size_t len);
int sys_open(const char *, int oflags);
int sys_close(int fd);
off_t sys_lseek(int fd, off_t off, int whence);
int sys_stat(const char *path, struct stat *buf);
int sys_lstat(const char *path, struct stat *buf);
int sys_fstat(int fd, struct stat *buf);
int sys_brk(void *);
void *sys_sbrk(intptr_t);

#endif /* KERNEL_SYSCALLS_H */
