/**
 * @defgroup kernel_syscalls_i686 Syscalls - x86
 * @ingroup kernel_syscalls
 *
 * # x86 ABI
 *
 * For convenience, our kernel's ABI is the same as Linux's:
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
 * The return value is **always** stored inside the @c eax register.
 *
 * @{
 */

#ifndef KERNEL_ARCH_I686_SYSCALLS_H
#define KERNEL_ARCH_I686_SYSCALLS_H

#include <uapi/arch/i686/syscalls.h>

#endif /* KERNEL_ARCH_I684_SYSCALLS_H */

/** @} */
