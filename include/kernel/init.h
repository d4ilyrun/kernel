#ifndef KERNEL_INIT_H
#define KERNEL_INIT_H

#include <kernel/error.h>

#include <utils/compiler.h>

/*
 * Those functions are called as the very first thing during the startup phase.
 * They are called with interrupts disabled, paging disabled and no way to
 * allocate virtual memory.
 */
#define INIT_BOOTSTRAP bootstrap

/*
 * Called after the bootstrap phase. Interrupts and virtual memory management
 * is already configured at this stage, but interrupts are stil disabled.
 */
#define INIT_EARLY early

/*
 */
#define INIT_NORMAL normal
#define INIT_LATE late

#define INIT_STEPS \
    INIT_BOOTSTRAP,     \
    INIT_EARLY,         \
    INIT_NORMAL,        \
    INIT_LATE

enum init_step {
    INIT_STEP_BOOTSTRAP,
    INIT_STEP_EARLY,
    INIT_STEP_NORMAL,
    INIT_STEP_LATE
};

struct initcall {
    const char *name;
    error_t (*call)(void); /** Initcall function */
};

/**
 * Initcalls from the same step are placed in a section together
 * by the linkerscript. This structure defines the start and end
 * boundaries of such sections.
 */
struct initcall_section {
    struct initcall *start;
    struct initcall *end;
};

#define DECLARE_INITCALL(_step, _function)        \
    MAYBE_UNUSED                                  \
    SECTION(".data.init." stringify(_step))      \
    static struct initcall __init_##_function = { \
        .name = stringify(_function),             \
        .call = _function,                        \
    }

/** Call all registered initcalls of a given step. */
void initcall_do_level(enum init_step);

#endif /* KERNEL_INIT_H */
