#include <kernel/error.h>
#include <kernel/process.h>
#include <kernel/signal.h>
#include <kernel/syscalls.h>
#include <kernel/logger.h>

/*
 * Setup user stack to execute the given signal handler when returning
 * to userland.
 */
error_t
arch_signal_deliver_catch(struct thread *thread,
                          const struct signal_action *sig_action,
                          const struct signal_context *sig_ctx)
{
    sig_sa_sigaction_t handler;
    struct interrupt_frame *interrupt_frame;
    struct signal_frame *signal_frame;
    void *stack;

     /*
     * We do not jump directly onto the handler but instead onto a stub
     * that calls sigreturn() after executing it. This stub must be
     * configured by the process using sigsethandler().
     */
    handler = READ_ONCE(thread->process->sig_handler);
    if (!handler)
        return E_INVAL;

    interrupt_frame = ((void *)thread->context.esp0) - sizeof(struct interrupt_frame);
    stack = (void *)interrupt_frame->frame.esp;

    stack -= sizeof(*signal_frame);
    signal_frame = stack;
    memset(signal_frame, 0, sizeof(struct signal_frame));

    /*
     * Setup user context used by sigreturn.
     *
     * Save the original set of registers since those will eventually
     * be orverwritten when exiting back from the userland handler to kernel
     * mode.
     *
     * Save sa_handler so the stub handler knows which function to call.
     *
     * Save the current signal mask to restore it in sigreturn() if the
     * signal handler exits normally (see sigaction()).
     */
    signal_frame->ucontext.regs = interrupt_frame->regs;
    signal_frame->ucontext.interrupt_frame = interrupt_frame->frame;
    signal_frame->ucontext.sa_handler = sig_action->sa_action.sa_handler;
    signal_frame->ucontext.sig_blocked = thread->sig_blocked;

    /*
     * Setup siginfo.
     */
    memcpy(&signal_frame->siginfo, &sig_ctx->si_info, sizeof(siginfo_t));

    /*
     * Setup arguments for the handler.
     */
    signal_frame->p_ucontext = &signal_frame->ucontext;
    signal_frame->p_siginfo = &signal_frame->siginfo;
    signal_frame->signo = sig_ctx->si_signo;

    /*
     * Update the interrupt frame so that the handler is called
     * when attempting to return to userland.
     */
    stack -= sizeof(void *);
    interrupt_frame->frame.esp = (u32)stack;
    interrupt_frame->frame.eip = (u32)handler;

    return E_SUCCESS;
}

/*
 * Return from a signal handler back to the normal process flow.
 */
int sys_sigreturn(struct user_signal_context *sig_uctx)
{
    struct interrupt_frame *interrupt_frame;

    /*
     * Restore the original interrupt frame (pre-signal) and signal mask.
     */
    interrupt_frame = ((struct interrupt_frame *)current->context.esp0) - 1;
    interrupt_frame->regs = sig_uctx->regs;
    interrupt_frame->frame = sig_uctx->interrupt_frame;
    current->sig_blocked = sig_uctx->sig_blocked;

    return E_SUCCESS;
}
