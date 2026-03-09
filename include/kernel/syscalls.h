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

#include <kernel/types.h>

#include <dailyrun/syscalls.h>

#include <utils/macro.h>

#include <stddef.h>
#include <sys/stat.h>

typedef struct syscall_args {
    u32 nr;
    u32 arg1, arg2, arg3, arg4, arg5, arg6;
} syscall_args_t;

#define SYSCALL_NUMBER(name, vector, ...) CONCAT(SYS_, name) = vector,
#define SYSCALL_FUNCTION(name, vector, argc, type, ret_type, ...) \
    ret_type CONCAT(sys_, name)(__VA_ARGS__);

/** The list of available syscall vectors.
 *  @enum syscall_nr
 */
enum syscal_nr {
    DEFINE_SYSCALLS(SYSCALL_NUMBER)
    SYSCALL_COUNT
};

/* SYSCALLS HANDLER */

DEFINE_SYSCALLS(SYSCALL_FUNCTION)

#endif /* KERNEL_SYSCALLS_H */
