#define LOG_DOMAIN "syscalls"

#include <kernel/devices/timer.h>
#include <kernel/devices/uart.h>
#include <kernel/error.h>
#include <kernel/init.h>
#include <kernel/interrupts.h>
#include <kernel/logger.h>
#include <kernel/syscalls.h>

struct syscall {
    /// The name of the syscall
    const char *name;
    /// The function that handles this syscall
    /// Cannot use a generic type, as all syscalls do not have the
    /// same signature
    void *handler;
    /// The number of arguments to the syscall
    int arg_count;
};

/** Parse the sycall's arguments from the arch-specific interrupt frame */
void arch_syscall_get_args(interrupt_frame *frame, syscall_args_t *args);

/** Set the value to be returned by the syscall */
void arch_syscall_set_return_value(interrupt_frame *frame, u32 value);

#define DECLARE_SYSCALL(_sc, _name, _handler, _arg_count) \
    [SYS_##_sc] = {_name, (void *)_handler, _arg_count}

/** The list of all the available syscalls and their respective handler */
static const struct syscall syscalls[SYSCALL_COUNT] = {
    DECLARE_SYSCALL(EXIT, "exit", sys_exit, 1),
    DECLARE_SYSCALL(FORK, "fork", sys_fork, 0),
    DECLARE_SYSCALL(READ, "read", sys_read, 3),
    DECLARE_SYSCALL(WRITE, "write", sys_write, 3),
    DECLARE_SYSCALL(OPEN, "open", sys_open, 2),
    DECLARE_SYSCALL(CLOSE, "close", sys_close, 1),
    DECLARE_SYSCALL(LSEEK, "lseek", sys_lseek, 3),
    DECLARE_SYSCALL(GETPID, "getpid", sys_getpid, 0),
    DECLARE_SYSCALL(STAT, "stat", sys_stat, 2),
    DECLARE_SYSCALL(LSTAT, "lstat", sys_lstat, 2),
    DECLARE_SYSCALL(FSTAT, "fstat", sys_fstat, 2),
    DECLARE_SYSCALL(BRK, "brk", sys_brk, 1),
    DECLARE_SYSCALL(SBRK, "sbrk", sys_sbrk, 1),
    DECLARE_SYSCALL(KILL, "kill", sys_kill, 2),
};

#define DO_SYSCALL_0(_syscall) (((u32(*)(void))_syscall)())
#define DO_SYSCALL_1(_syscall, _arg1) \
    (((u32(*)(void *))_syscall)((void *)_arg1))
#define DO_SYSCALL_2(_syscall, _arg1, _arg2) \
    (((u32(*)(void *, void *))_syscall)((void *)_arg1, (void *)_arg2))
#define DO_SYSCALL_3(_syscall, _arg1, _arg2, _arg3)                           \
    (((u32(*)(void *, void *, void *))_syscall)((void *)_arg1, (void *)_arg2, \
                                                (void *)_arg3))

/**
 * Find a syscall's name when porting a program that was compiled for Linux.
 *
 * The syscall
 */
extern const char *syscall_linux_syscalls[];

/** Perform a syscall */
static u32 syscall(void *frame)
{
    const struct syscall *syscall;
    syscall_args_t args;
    u32 ret;

    arch_syscall_get_args(frame, &args);

    if (args.nr >= SYSCALL_COUNT || !syscalls[args.nr].handler) {
        log_err("Unimplemented syscall: '%s' (%d)",
                syscall_linux_syscalls[args.nr], args.nr);
        return -E_NOT_IMPLEMENTED;
    }

    syscall = &syscalls[args.nr];

    switch (syscall->arg_count) {
    case 0:
        ret = DO_SYSCALL_0(syscall->handler);
        break;
    case 1:
        ret = DO_SYSCALL_1(syscall->handler, args.arg1);
        break;
    case 2:
        ret = DO_SYSCALL_2(syscall->handler, args.arg1, args.arg2);
        break;
    case 3:
        ret = DO_SYSCALL_3(syscall->handler, args.arg1, args.arg2, args.arg3);
        break;
    default:
        log_err("%s: unsupported arg count (%d)", syscall->name,
                syscall->arg_count);
        ret = -E_NOT_SUPPORTED;
    }

    arch_syscall_set_return_value(frame, ret);

    return ret;
}

static error_t syscall_init(void)
{
    // The switch from user to kernel mode is performed by triggering the
    // 128th interrupt (the most common way).
    interrupts_set_handler(0x80, syscall, NULL);
    return E_SUCCESS;
}

DECLARE_INITCALL(INIT_LATE, syscall_init);
