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

/** The interrupt number used to trigger a syscall */
#define SYSCALL_INTERRUPT_NR 0x80

/** The list of available syscall vectors.
 *
 *  To make porting already existing programs easier, our syscall
 *  numbers are copied 1:1 from Linux.
 *
 *  @note Syscall numbers may differ depending on the architecture.
 *
 *  @enum syscall_nr
 */
typedef enum syscall_nr {
    SYS_EXIT = 1,    /*!< exit() */
    SYS_FORK = 2,    /*!< fork() */
    SYS_READ = 3,    /*!< read() */
    SYS_WRITE = 4,   /*!< write() */
    SYS_OPEN = 5,    /*!< open() */
    SYS_CLOSE = 6,   /*!< close() */
    SYS_LSEEK = 19,  /*!< lseek() */
    SYS_GETPID = 20, /*!< getpid() */
    SYS_KILL = 37,   /*!< kill() */
    SYS_BRK = 45,    /*!< brk() */
    SYS_STAT = 106,  /*!< stat() */
    SYS_LSTAT = 107, /*!< lstat() */
    SYS_FSTAT = 108, /*!< fstat() */
    SYS_SBRK = 463,  /*!< lstat() */
    SYSCALL_COUNT
} syscall_nr;

#endif /* KERNEL_ARCH_I684_SYSCALLS_H */

/** @} */
