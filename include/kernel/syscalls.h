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
 * I'll try to stick to something similar to POSIX syscalls as much as possible,
 * as it is what I've already become accustomed to, and that this should make
 * eventually porting programs less of a hassle.
 *
 * Some core POSIX concepts are still missing, so some syscalls might be missing
 * crucial arguments (namely file descriptors). This will surely change in the
 * future once we implement the necessary aspects of the kernel.
 *
 * So, as always, this API is subject to change along with the kernel.
 *
 * ## ABI
 *
 * for convenience, our kernel's ABI is the same as Linux's:
 *
 * ### x86
 *
 * The arguments are passed in the following order:
 * > eax, ebx, ecx, edx, esi, edi, ebp
 *
 * The first argument (eax) is **always** the number of the syscall to call.
 *
 * @{
 */
#ifndef KERNEL_SYSCALLS_H
#define KERNEL_SYSCALLS_H

#include <kernel/types.h>

#include <stddef.h>
#include <stdint.h>

/** The interrupt used to trigger a syscall */
#define SYSCALL_INTERRUPT_NR 0x80

/** The list of available syscall vectors
 *  @enum syscall_nr
 */
typedef enum syscall_nr {
    SYS_WRITE = 4,
    SYSCALL_COUNT
} syscall_nr;

typedef struct syscall_args {
    u32 nr;
    u32 arg1, arg2, arg3, arg4, arg5, arg6;
} syscall_args_t;

/** Initialize the sycsall API */
void syscall_init(void);

#endif /* KERNEL_SYSCALLS_H */
