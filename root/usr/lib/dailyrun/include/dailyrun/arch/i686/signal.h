#ifndef DAILYRUN_ARCH_I686_SIGNAL_H
#define DAILYRUN_ARCH_I686_SIGNAL_H

#include <dailyrun/arch/i686/cpu.h>

/*
 * User context structure pushed onto the stack before sending a signal
 * and re-used by sigreturn() to restore the original program state after
 * exiting the signal handler.
 */
struct user_signal_context {
    struct x86_regs             regs;
    struct x86_interrupt_frame  interrupt_frame;
    void                        *sa_handler;
    sigset_t                    sig_blocked;
};

typedef struct user_signal_context ucontext_t;

#endif /* DAILYRUN_ARCH_I686_SIGNAL_H */
