#define LOG_DOMAIN "syscalls"

#include <kernel/devices/timer.h>
#include <kernel/devices/uart.h>
#include <kernel/error.h>
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
    DECLARE_SYSCALL(FORK, "fork", sys_fork, 0)};

#define DO_SYSCALL_0(_syscall) (((u32(*)(void))_syscall)())

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
    default:
        log_err("%s: unsupported arg count (%d)", syscall->name,
                syscall->arg_count);
        ret = -E_NOT_SUPPORTED;
    }

    arch_syscall_set_return_value(frame, ret);

    return ret;
}

void syscall_init(void)
{
    // The switch from user to kernel mode is performed by triggering the
    // 128th interrupt (the most common way).
    interrupts_set_handler(0x80, syscall, NULL);
}
