/*
 * # x86 syscall ABI
 *
 * For convenience our kernel uses the same syscall ABI as Linux.
 *
 * ## Arguments
 *
 * The arguments are passed in the following order:
 * > eax, ebx, ecx, edx, esi, edi, ebp
 *
 * The first argument (eax) is **always** the number of the syscall to call.
 *
 * ## Return value
 *
 * The return value is **always** stored inside the eax register.
 *
 */

#ifndef _DAILYRUN_I686_SYSCALLS_H
#define _DAILYRUN_I686_SYSCALLS_H

/** The interrupt number used to trigger a syscall */
#define SYSCALL_INTERRUPT_NR 0x80

#include <dailyrun/syscalls.h>

#endif /* _DAILYRUN_I686_SYSCALLS_H */
