#define LOG_DOMAIN "signal"

#include <kernel/error.h>
#include <kernel/init.h>
#include <kernel/logger.h>
#include <kernel/memory/slab.h>
#include <kernel/process.h>
#include <kernel/signal.h>
#include <kernel/spinlock.h>
#include <kernel/syscalls.h>

static struct kmem_cache *kmem_cache_sigset;
static struct kmem_cache *kmem_cache_sigctx;

/*
 * Default action taken when
 */
enum signal_action_type {
    SIG_ACTION_TERMINATE,
    SIG_ACTION_ABORT,
    SIG_ACTION_IGNORE,
    SIG_ACTION_STOP,
    SIG_ACTION_CONTINUE,
    SIG_ACTION_CATCH,
};

/*
 * Default signal action performed by SIG_DFL.
 */
static const enum signal_action_type signal_actions_default[SIGNAL_COUNT] = {
    [SIGABRT]   = SIG_ACTION_ABORT,
    [SIGALRM]   = SIG_ACTION_TERMINATE,
    [SIGBUS]    = SIG_ACTION_ABORT,
    [SIGCHLD]   = SIG_ACTION_IGNORE,
    [SIGCONT]   = SIG_ACTION_CONTINUE,
    [SIGFPE]    = SIG_ACTION_ABORT,
    [SIGHUP]    = SIG_ACTION_TERMINATE,
    [SIGILL]    = SIG_ACTION_ABORT,
    [SIGINT]    = SIG_ACTION_TERMINATE,
    [SIGKILL]   = SIG_ACTION_TERMINATE,
    [SIGPIPE]   = SIG_ACTION_TERMINATE,
    [SIGQUIT]   = SIG_ACTION_ABORT,
    [SIGSEGV]   = SIG_ACTION_ABORT,
    [SIGSTOP]   = SIG_ACTION_STOP,
    [SIGTERM]   = SIG_ACTION_TERMINATE,
    [SIGTSTP]   = SIG_ACTION_STOP,
    [SIGTTIN]   = SIG_ACTION_STOP,
    [SIGTTOU]   = SIG_ACTION_STOP,
    [SIGUSR1]   = SIG_ACTION_TERMINATE,
    [SIGUSR2]   = SIG_ACTION_TERMINATE,
    [SIGWINCH]  = SIG_ACTION_IGNORE,
    [SIGSYS]    = SIG_ACTION_ABORT,
    [SIGTRAP]   = SIG_ACTION_ABORT,
    [SIGURG]    = SIG_ACTION_IGNORE,
    [SIGVTALRM] = SIG_ACTION_TERMINATE,
    [SIGXCPU]   = SIG_ACTION_ABORT,
    [SIGXFSZ]   = SIG_ACTION_ABORT,
};

/*
 *
 */
struct signal_set *signal_set_alloc(void)
{
    return kmem_cache_alloc(kmem_cache_sigset, 0);
}

/*
 * Used by the kmem_cache API.
 */
static void signal_set_constructor(void *obj)
{
    struct signal_set *set = obj;

    INIT_SPINLOCK(set->lock);
}

/*
 *
 */
void signal_set_free(struct signal_set *set)
{
    if (!set)
        return;

    kmem_cache_free(kmem_cache_sigset, set);
}

/*
 * Reset all installed signal actions.
 * This should be called inside execve().
 */
void signal_set_reset(struct signal_set *set)
{
    spinlock_acquire(&set->lock);

    for (int signo = SIGNAL_MIN; signo <= SIGNAL_MAX; ++signo) {
        struct sigaction *sa_action = &set->sig_actions[signo].sa_action;

        sa_action->sa_flags = 0;
        sa_action->sa_mask = 0;

        /*
         * Signals set to the default action, or setto be caught, must be set
         * to the default action in the new process.
         *
         * Signals set to be ignored stay that way. The only special case
         * being SIGCHLD for which the choice is implementation defined.
         * In our case we always reset SIGCHLD back to the default action.
         */
        if (sa_action->sa_handler != SIG_IGN || signo == SIGCHLD)
            sa_action->sa_handler = SIG_DFL;
    }

    spinlock_release(&set->lock);
}

/*
 *
 */
static void signal_set_init(struct signal_set *set)
{
    for (int signo = SIGNAL_MIN; signo <= SIGNAL_MAX; ++signo)
        set->sig_actions[signo].sa_action.sa_handler = SIG_DFL;
}

/*
 *
 */
struct signal_set *signal_set_clone(struct signal_set *set)
{
    struct signal_set *new;

    new = signal_set_alloc();
    if (new == NULL)
        return new;


    /* init inherits a null sigset from kproc */
    if (set == NULL)
        signal_set_init(new);
    else
        memcpy(new->sig_actions, set->sig_actions, sizeof(new->sig_actions));

    return new;
}

/*
 *
 */
static struct signal_context *signal_context_new(const siginfo_t *sig_info)
{
    struct signal_context *sig_ctx;

    if (sig_info->si_signo < SIGNAL_MIN || sig_info->si_signo > SIGNAL_MAX)
        return PTR_ERR(E_INVAL);

    sig_ctx = kmem_cache_alloc(kmem_cache_sigctx, 0);
    if (!sig_ctx)
        return PTR_ERR(E_NOMEM);

    sig_ctx->si_info = *sig_info;
    sig_ctx->si_signo = sig_info->si_signo;

    return sig_ctx;
}

/*
 *
 */
static void signal_context_destroy(struct signal_context *sig_ctx)
{
    llist_remove(&sig_ctx->this);

    kmem_cache_free(kmem_cache_sigctx, sig_ctx);
}

/*
 *
 */
static void signal_queue_push(struct signal_queue *queue,
                              struct signal_context *sig_ctx)
{
    spinlock_acquire(&queue->lock);
    llist_add_tail(&queue->signals, &sig_ctx->this);
    sigaddset(&queue->pending, sig_ctx->si_signo);
    spinlock_release(&queue->lock);
}

/*
 * Pop and return the first deliverable signal inside the queue.
 */
struct signal_context *signal_queue_pop(struct signal_queue *queue,
                                        sigset_t blocked)
{
    struct signal_context *sig_ctx = NULL;

    locked_scope (&queue->lock)
    {
        struct signal_context *entry = NULL;

        if (llist_is_empty(&queue->signals))
            return NULL;

        FOREACH_LLIST_ENTRY(entry, &queue->signals, this) {
            /*
             * Blocked signals whose associated action is to ignore the signal
             * are left inside the queue until they can be 'delivered'.
             * This behaviour is implementation defined (see POSIX 2.4.1).
             */
            if (!sigismember(&blocked, entry->si_signo)) {
                sig_ctx = entry;
                break;
            }
        }

        if (!sig_ctx)
            return NULL;

        /*
         * Check if the queue still contains other pending signals with
         * the same signal number to keep the mask of pending signals updated.
         */
        FOREACH_LLIST_ENTRY_REVERSE(entry, &queue->signals, this) {
            if (entry->si_signo == sig_ctx->si_signo) {
                if (entry == sig_ctx)
                    sigdelset(&queue->pending, sig_ctx->si_signo);
                break;
            }
        }

        llist_remove(&sig_ctx->this);
    }

    return sig_ctx;
}

/*
 * Remove and free all instances of a given signal inside a signal queue.
 *
 * @return The number of signals removed.
 */
static size_t signal_queue_flush_signals(struct signal_queue *queue,
                                         sigset_t to_remove)
{
    struct signal_context *cur;
    struct signal_context *next;
    size_t count = 0;

    spinlock_acquire(&queue->lock);
    FOREACH_LLIST_ENTRY_SAFE(cur, next, &queue->signals, this) {
        if (sigismember(&to_remove, cur->si_signo)) {
            signal_context_destroy(cur);
            count += 1;
        }
    }
    queue->pending &= ~to_remove;
    spinlock_release(&queue->lock);

    return count;
}

/*
 * Remove and free all signals present inside a signal queue.
 *
 * @return The number of signals inside the queue.
 */
size_t signal_queue_flush(struct signal_queue *queue)
{
    struct signal_context *cur;
    struct signal_context *next;
    size_t count = 0;

    spinlock_acquire(&queue->lock);
    FOREACH_LLIST_ENTRY_SAFE(cur, next, &queue->signals, this) {
        signal_context_destroy(cur);
        count += 1;
    }
    queue->pending = 0; /* clear mask */
    spinlock_release(&queue->lock);

    return count;
}

/*
 * POSIX 2.4.3
 *
 * The process is terminated as if by a call to _exit(), except that the status
 * made available to wait(), waitid(), and waitpid() indicates abnormal
 * termination by the signal.
 */
static void
signal_deliver_terminate(struct thread *thread,
                         const struct signal_action *sig_action MAYBE_UNUSED,
                         const struct signal_context *sig_ctx)
{
    u16 status;

    status = (sig_ctx->si_signo << 8) | sig_ctx->si_signo;
    process_kill(thread->process, status);

    assert_not_reached();
}

/*
 *
 */
static void
signal_deliver_abort(struct thread *thread,
                     const struct signal_action *sig_action MAYBE_UNUSED,
                     const struct signal_context *sig_ctx)
{
    u16 status;

    not_implemented("abort: core dump");
    status = (sig_ctx->si_signo << 8) | 0x80;
    process_kill(thread->process, status);

    assert_not_reached();
}

/*
 * POSIX 2.4.3 - Delivery of the signal shall have no effect on the process.
 */
static void
signal_deliver_ignore(struct thread *thread,
                      const struct signal_action *sig_action,
                      const struct signal_context *sig_ctx)
{
    UNUSED(thread);
    UNUSED(sig_action);
    UNUSED(sig_ctx);
}

/*
 * TODO: SIGSTOP
 */
static void
signal_deliver_stop(struct thread *thread,
                    const struct signal_action *sig_action,
                    const struct signal_context *sig_ctx)
{
    not_implemented("stop");
    signal_deliver_ignore(thread, sig_action, sig_ctx);
}

/*
 * TODO: SIGCONT
 */
static void
signal_deliver_continue(struct thread *thread,
                        const struct signal_action *sig_action,
                        const struct signal_context *sig_ctx)
{
    not_implemented("continue");
    signal_deliver_ignore(thread, sig_action, sig_ctx);
}

/*
 * Catch signal in a userland function.
 */
static void
signal_deliver_catch(struct thread *thread,
                     const struct signal_action *sig_action,
                     const struct signal_context *sig_ctx)
{
    if (arch_signal_deliver_catch(thread, sig_action, sig_ctx) != E_SUCCESS)
        return;

    /* Install new signal mask configured by sigaction(). */
    thread->sig_blocked |= sig_action->sa_action.sa_mask;
}

typedef void (*signal_deliver_func_t)(struct thread *,
                                      const struct signal_action *,
                                      const struct signal_context *);

static const signal_deliver_func_t signal_deliver_functions[] = {
    [SIG_ACTION_TERMINATE]  = signal_deliver_terminate,
    [SIG_ACTION_ABORT]      = signal_deliver_abort,
    [SIG_ACTION_IGNORE]     = signal_deliver_ignore,
    [SIG_ACTION_STOP]       = signal_deliver_stop,
    [SIG_ACTION_CONTINUE]   = signal_deliver_continue,
    [SIG_ACTION_CATCH]      = signal_deliver_catch,
};

/*
 * Main signal delivery function.
 *
 * It determines the final action to perform for the given signal based on
 * previous calls to sigaction(), and handles signal delivery accordingly.
 *
 * This function does not check whether a signal is blocked, this verification
 * should be performed by the caller (e.g. signal_queue_pop).
 */
void signal_deliver(struct thread *thread, struct signal_context *sig_ctx)
{
    struct signal_action *sig_action;
    struct signal_set *sig_set;
    enum signal_action_type action_type;
    signal_deliver_func_t deliver;
    int signo = sig_ctx->si_signo;

    WARN_ON(thread != current);

    sig_set = thread->process->sig_set;
    spinlock_acquire(&sig_set->lock);
    sig_action = &sig_set->sig_actions[signo];

    /*
     * Determine the final action to perform.
     */
    action_type = SIG_ACTION_CATCH;
    if (sig_action->sa_action.sa_handler == SIG_IGN) {
        action_type = SIG_ACTION_IGNORE;
    } else if (sig_action->sa_action.sa_handler == SIG_DFL) {
        action_type = signal_actions_default[signo];
    }

    deliver = signal_deliver_functions[action_type];
    deliver(thread, sig_action, sig_ctx);

    spinlock_release(&sig_set->lock);

    signal_context_destroy(sig_ctx);
}

/*
 *
 */
static inline sigset_t signal_compute_mask(sigset_t mask)
{
    /*
     * SIGKILL and SIGSTOP cannot be masked.
     *
     * This should be enforced without raising an error.
     */
    sigdelset(&mask, SIGKILL);
    sigdelset(&mask, SIGSTOP);

    return mask;
}

/*
 * Compute whether one process has permission to signal another.
 */
static inline bool
signal_can_generate(struct thread *sender, struct thread *receiver)
{
    struct user_creds *sender_creds = NULL;
    struct user_creds *receiver_creds = NULL;
    bool can_generate = true;

    /*
     * Kernel threads cannot be signaled.
     */
    if (thread_is_kernel(receiver))
        return false;

    if (thread_is_kernel(sender))
        return true;

    if (sender->process == receiver->process)
        return true;

    sender_creds = creds_get(sender->process->creds);
    if (creds_is_root(sender_creds))
        goto out;

    receiver_creds = creds_get(receiver->process->creds);
    can_generate = (sender_creds->euid == receiver_creds->euid) ||
                   (sender_creds->ruid == receiver_creds->ruid);

out:
    creds_put(sender_creds);
    creds_put(receiver_creds);
    return can_generate;
}

/*
 * Send signal to another process or thread.
 */
error_t signal_generate(struct thread *thread, const siginfo_t *sig_info,
                        bool to_thread)
{
    struct signal_context *sig_ctx;
    struct signal_queue *queue;
    struct process *process = thread->process;
    struct thread *child;
    sigset_t to_remove;

    if (!signal_can_generate(current, thread))
        return E_PERM;

    sig_ctx = signal_context_new(sig_info);
    if (IS_ERR(sig_ctx))
        return ERR_FROM_PTR(sig_ctx);

    /*
     * FIXME: Potential TOCTOU error!
     *
     * If another processor pulls the rug from under us and the process
     * or the thread is killed and released before we arrive here.
     *
     * Threads should be refcounted to avoid this, and we should be able to
     * increase a process's reference count also.
     *
     * Let's simply hold the process' lock for now as a best effort...
     */
    spinlock_acquire (&process->lock);

    /*
     * When any stop signal is generated, all pending SIGCONT signals
     * for that process or any of the threads within that process shall be
     * discarded.
     *
     * When SIGCONT is generated, all pending stop signals shall be discarded.
     */

    sigemptyset(&to_remove);
    switch (sig_info->si_signo) {
    case SIGSTOP:
    case SIGTSTP:
    case SIGTTIN:
    case SIGTTOU:
        sigaddset(&to_remove, SIGCONT);
        break;
    case SIGCONT:
        sigaddset(&to_remove, SIGSTOP);
        sigaddset(&to_remove, SIGTSTP);
        sigaddset(&to_remove, SIGTTIN);
        sigaddset(&to_remove, SIGTTOU);
        break;
    default:
        break;
    }

    if (to_remove) {
        signal_queue_flush_signals(&process->sig_pending, to_remove);
        FOREACH_LLIST_ENTRY(child, &process->children, this_proc)
            signal_queue_flush_signals(&child->sig_pending, to_remove);
    }

    queue = to_thread ? &thread->sig_pending : &process->sig_pending;
    signal_queue_push(queue, sig_ctx);

    spinlock_release(&process->lock);

    return E_SUCCESS;
}

/*
 * Generate a process-wide signal.
 */
error_t signal_process(struct process *process, const siginfo_t *sig_info)
{
    struct thread *thread;

    thread = llist_first_entry(&process->threads, typeof(*thread), this_proc);
    return signal_generate(thread, sig_info, false);
}

/*
 * Generate a thread-specific signal.
 */
error_t signal_thread(struct thread *thread, const siginfo_t *sig_info)
{
    return signal_generate(thread, sig_info, true);
}

/*
 * Generate a signal to multiple processes.
 */
static error_t signal_brodcast(pid_t pid, siginfo_t *sig_info)
{
    struct process *process;

    /* TODO: Process Group IDs. */
    if (pid < 0)
        return -E_NOT_SUPPORTED;

    spinlock_acquire(&processes_list_lock);

    FOREACH_LLIST_ENTRY(process, &processes_list, this_global) {
        if (pid == 0 && process->pid == pid)
            signal_process(process, sig_info);
    }

    spinlock_release(&processes_list_lock);

    return E_SUCCESS;

}

/*
 *
 */
int sys_kill(pid_t pid, int signal)
{
    siginfo_t sig_info;

    if (signal < SIGNAL_MIN || signal > SIGNAL_MAX)
        return -E_INVAL;

    memset(&sig_info, 0, sizeof(sig_info));
    sig_info.si_signo = signal;
    sig_info.si_code = SI_USER;

    if (pid > 0) {
        struct process *process;

        /*
         * FIXME: Process can also be a zombie.
         */
        process = process_find_by_pid(pid);
        if (!process)
            return -E_SRCH;

        return -signal_process(process, &sig_info);
    }

    return signal_brodcast(pid, &sig_info);
}

/*
 *
 */
static error_t signal_action_configure(struct signal_set *set, int signo,
                                       struct signal_action *sig_action,
                                       struct signal_action *old_sig_action)
{
    struct sigaction *sa_action = &sig_action->sa_action;

    /*
     * Attempts to set a signal that cannot be caught/ignored return an error.
     *
     * This is an implementation defined behaviour (POSIX - sigaction).
     */
    if (signo == SIGKILL || signo == SIGSTOP)
        return E_INVAL;

    if (signo < SIGNAL_MIN || signo > SIGNAL_MAX)
        return E_INVAL;

    /*
     * Compute the final mask.
     */
    sa_action->sa_mask = signal_compute_mask(sa_action->sa_mask);

    locked_scope (&set->lock)
    {
        struct signal_action *orig;

        orig = &set->sig_actions[signo];
        if (old_sig_action)
            *old_sig_action = *orig;
        *orig = *sig_action;
    }

    return E_SUCCESS;
}

/*
 *
 */
int sys_sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
    struct signal_action sig_action;
    struct signal_action old_sig_action;
    error_t err;

    memset(&sig_action, 0, sizeof(sig_action));
    sig_action.sa_action = *act;

    err = signal_action_configure(current->process->sig_set, sig, &sig_action,
                                  &old_sig_action);
    if (err)
        return err;

    if (oact)
        *oact = old_sig_action.sa_action;

    return E_SUCCESS;
}

/*
 * Change or read the signal mask of the calling thread.
 *
 * TODO: EFAULT: The set or oldset argument points outside the process's
 *               allocated address space.
 */
int sys_sigprocmask(int how, const sigset_t *set, sigset_t *old, size_t getsize)
{
    if (old) {
        if (getsize != sizeof(*old))
            return -E_INVAL;
        *old = current->sig_blocked;
    }

    if (!set)
        return 0;

    /* Clear out unmaskable signals. */
    *old = signal_compute_mask(*old);

    switch (how) {
        case SIG_BLOCK:
            current->sig_blocked |= *old;
            break;
        case SIG_UNBLOCK:
            current->sig_blocked &= ~(*old);
            break;
        case SIG_SETMASK:
            current->sig_blocked = *old;
            break;
        default:
            return -E_INVAL;
    }

    return 0;
}

/*
 * Fetch the set of pending signals that are blocked from delivery to the
 * calling thread.
 */
int sys_sigpending(sigset_t *set)
{
    sigset_t pending = 0;

    pending |= current->sig_pending.pending;
    pending |= current->process->sig_pending.pending;
    *set = pending & current->sig_blocked;

    return 0;
}

/*
 * Configure the stub signal handler.
 *
 * The stub handler calls the real signal handler and performs the necessary
 * actions afterwards to restore the original state of the usersack
 * (i.e. calling sigreturn()).
 *
 * @args handler The stub handler
 *
 * @return EINVAL The handler function is invalid.
 */
int sys_sigsethandler(sig_sa_sigaction_t handler)
{
    if (IS_KERNEL_ADDRESS(handler))
        return -E_INVAL;

    /* arch_signal_deliver_catch() must use READ_ONCE(). */
    WRITE_ONCE(current->process->sig_handler, handler);

    return 0;
}

/*
 * Initialize the signal API.
 */
static error_t init_signals(void)
{
    kmem_cache_sigset = kmem_cache_create("signal_set",
                                          sizeof(struct signal_set), 1,
                                          signal_set_constructor, NULL);
    kmem_cache_sigctx = kmem_cache_create("signal",
                                          sizeof(struct signal_context), 1,
                                          NULL, NULL);

    if (IS_ERR(kmem_cache_sigset))
        return ERR_FROM_PTR(kmem_cache_sigset);

    if (IS_ERR(kmem_cache_sigctx)) {
        kmem_cache_destroy(kmem_cache_sigset);
        return ERR_FROM_PTR(kmem_cache_sigctx);
    }

    return E_SUCCESS;
}

DECLARE_INITCALL(INIT_NORMAL, init_signals);
