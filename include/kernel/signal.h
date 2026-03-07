/*
 * Posix signals
 *
 * Reference:
 *  * POSIX 2.4 - Signal Concepts
 */

#ifndef KERNEL_SIGNAL_H
#define KERNEL_SIGNAL_H

#include <kernel/spinlock.h>
#include <kernel/atomic.h>
#include <kernel/error.h>

#include <libalgo/queue.h>

#include <sys/signal.h>

#if ARCH == i686
#include <kernel/arch/i686/signal.h>
#endif

struct thread;
struct process;

#define SIGNAL_MIN 1
#define SIGNAL_MAX (SIGNAL_COUNT - 1)
#define SIGNAL_COUNT NSIG

/*
 * A pending signal.
 *
 * This struct holds information about a pending signals that is necessary
 * to take decisions and fill the structures during signal delivery.
 */
struct signal_context {
    node_t      this; /* used by struct signal_queue */
    siginfo_t   si_info;
    int         si_signo;
};

/*
 * A queue of pending signals.
 *
 * There exists one queue of pending signal per process AND per thread.
 */
struct signal_queue {
    spinlock_t  lock;
    llist_t     signals; /* struct signal_context */
    sigset_t    pending;
};

static inline void signal_queue_init(struct signal_queue *queue)
{
    INIT_SPINLOCK(queue->lock);
    INIT_LLIST(queue->signals);
}

/*
 * Reflects what is specified in sigaction().
 */
struct signal_action {
    struct sigaction sa_action;
};

/*
 *
 */
struct signal_set {
    spinlock_t            lock; /* should be held when accessing this structure. */
    struct signal_action  sig_actions[SIGNAL_COUNT];
};

/*
 * Frame pushed onto the stack before calling the signal handler.
 */
struct signal_frame {
    int         signo;
    siginfo_t   *p_siginfo;
    ucontext_t  *p_ucontext;
    /* Above are the arguments passed to the signal handler. */
    siginfo_t  siginfo;
    ucontext_t ucontext;
};

/** Free an existing signal set structure. */
void signal_set_free(struct signal_set *set);

/** Clone an existing signal set. */
struct signal_set *signal_set_clone(struct signal_set *set);

/** Reset all signal actions to their default value. */
void signal_set_reset(struct signal_set *set);

struct signal_context *signal_queue_pop(struct signal_queue *queue,
                                        sigset_t blocked);

/** Remove and free all signals present inside a signal queue. */
size_t signal_queue_flush(struct signal_queue *queue);

void signal_deliver(struct thread *thread, struct signal_context *sig_ctx);

/*
 * Generate a process-wide signal.
 */
error_t signal_process(struct process *process, const siginfo_t *sig_info);

/*
 * Generate a thread-specific signal.
 */
error_t signal_thread(struct thread *thread, const siginfo_t *sig_info);

/*
 * Arch-specific signal related functions.
 */

error_t arch_signal_deliver_catch(struct thread *, const struct signal_action *,
                                  const struct signal_context *);

#endif /* KERNEL_SIGNAL_H */
