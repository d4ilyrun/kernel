#define LOG_DOMAIN "syscalls"

#include <kernel/devices/uart.h>
#include <kernel/error.h>
#include <kernel/init.h>
#include <kernel/interrupts.h>
#include <kernel/logger.h>
#include <kernel/syscalls.h>
#include <kernel/timer.h>

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

#ifdef CONFIG_TRACE_SYSCALLS

#include <kernel/process.h>

static inline void trace_syscall_entry(const struct syscall *syscall)
{
    log_dbg("[%d] entering %s()", current->tid, syscall->name);
}

static inline void trace_syscall_exit(const struct syscall *syscall, int ret)
{
    log_dbg("[%d] exiting %s() with %d", current->tid, syscall->name, ret);
}

#else

#define trace_syscall_entry(...)
#define trace_syscall_exit(...)

#endif /* CONFIG_TRACE_SYSCALLS */

/** Parse the sycall's arguments from the arch-specific interrupt frame */
void arch_syscall_get_args(interrupt_frame *frame, syscall_args_t *args);

/** Set the value to be returned by the syscall */
void arch_syscall_set_return_value(interrupt_frame *frame, u32 value);

#define DECLARE_SYSCALL(name, vector, argc, ...) \
    [SYS_##name] = {#name, (void *)sys_##name, argc},

/** The list of all the available syscalls and their respective handler */
static const struct syscall syscalls[SYSCALL_COUNT] = {
    DEFINE_SYSCALLS(DECLARE_SYSCALL)
};

#define DO_SYSCALL_0(_syscall) (((u32(*)(void))_syscall)())
#define DO_SYSCALL_1(_syscall, _arg1) \
    (((u32(*)(void *))_syscall)((void *)_arg1))
#define DO_SYSCALL_2(_syscall, _arg1, _arg2) \
    (((u32(*)(void *, void *))_syscall)((void *)_arg1, (void *)_arg2))
#define DO_SYSCALL_3(_syscall, _arg1, _arg2, _arg3)                           \
    (((u32(*)(void *, void *, void *))_syscall)((void *)_arg1, (void *)_arg2, \
                                                (void *)_arg3))

/** Perform a syscall */
static u32 syscall(void *frame)
{
    const struct syscall *syscall;
    syscall_args_t args;
    u32 ret;

    arch_syscall_get_args(frame, &args);

    if (args.nr >= SYSCALL_COUNT || !syscalls[args.nr].handler) {
        log_err("Unimplemented syscall: %d", args.nr);
        return -E_NOT_IMPLEMENTED;
    }

    syscall = &syscalls[args.nr];
    trace_syscall_entry(syscall);

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
    trace_syscall_exit(syscall, ret);

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
