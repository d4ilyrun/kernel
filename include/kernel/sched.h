#pragma once

/**
 * @brief Scheduler implementation
 *
 * @defgroup scheduling Scheduling
 * @ingroup kernel
 *
 * # Scheduler
 *
 * The scheduler is responsible for ... well, scheduling proceses.
 *
 * It has to decide which threads to run next, and when, among all
 * currently running threads. This is what allows running multiple threads
 * on a single CPU.
 *
 * A good scheduler is crucial to the user's experience, and this is often what
 * makes the "feel" of the entire OS.
 *
 * ## Design
 *
 * For now, our scheduler uses a preemptive round-robin design.
 * It holds a list of currently running threads, called the runqueue,
 * and cycles between them at a regular interval.
 *
 * The interval is set currently set to 2MS, per thread, per cycle, and is
 * handled inside \ref irq_timer_handler. If the current thread is still
 * running when the current interval reaches its end, the next one takes its
 * place, and the timer is reset. This is called preemption.
 *
 * During the execution of a thread, it often needs to access some
 * resources, thus having to wait until the resource is available. When this is
 * the case, the thread is marked as \ref SCHED_WAITING using \ref
 * sched_block_current_thread, and we switch to the next available running
 * thread. Once the resource is available, the relevant interface has the
 * resposibility to notify the scheduler that the thread can be rescheduled,
 * using \ref sched_unblock_thread.
 *
 * ## Improvements
 *
 * * Use a separate timer for pre-emption, instead of mingling with the one used
 *   for timekeeping. This would allow for dynamically resetting the timer
 *   interrupt without sacrificing precision on ticks.
 *
 * * Priority levels. We would like to give more time to more "important"
 *   thread ideally. This would require multiple runqueues, but is a MUST
 *   have for any actual scheduler.
 *
 * * SMP: when activating multiprocessing, we want to setup a scheduler per
 *   core, and dispatch threads across them. This would also imply other smart
 *   opimizations (time stealing, ...)
 *
 * @{
 */

#include <kernel/process.h>

#include <utils/compiler.h>

// TODO: Remove this once we implemented another timer for the scheduler
extern bool scheduler_initialized;

/**
 * Reschedule the current thread
 *
 * This is the **main** function of the scheduler. It is called when we want to
 * switch to the next scheduled thread. It automatically reinserts the current
 * thread into the correct queue depending on its state.
 */
void schedule(void);

/** Forcibly reschedule the current thread */
void schedule_preempt(void);

/** Prevent the current thread from being pree-empted by the scheduler.
 *  @return Wether interrupts were previously enabled
 */
bool scheduler_preempt_disable(void);

/** Re-allow the current thread to be pre-empted.
 *  @param old_if_flag The state of the interrputs prior to locking
 */
void scheduler_preempt_enable(bool old_if_flag);

/** Add a new thread to be scheduled.
 *  When adding a new thread, its state will be set to @ref SCHED_RUNNING
 */
void sched_new_thread(thread_t *);

/** Create a new thread and instantly schedule it
 *  @see \ref sched_new_thread
 *       \ref thread_create
 */
static ALWAYS_INLINE void
sched_new_thread_create(thread_entry_t entrypoint, void *data, u32 flags)
{
    struct thread *thread = thread_spawn(current->process, entrypoint, data,
                                         NULL, flags);
    if (!IS_ERR(thread))
        sched_new_thread(thread);
}

/** Mark the thread as blocked
 *
 * A blocked thread cannot be rescheduled until it is explicitely unblocked
 * using @ref sched_unblock_thread()
 */
void sched_block_thread(struct thread *);

/** Unblock a currently blocked thread
 *
 * The thread is marked as @ref SCHED_RUNNING and is automatically added to the
 * appropriate runqueue.
 */
void sched_unblock_thread(thread_t *);

/** Block thread and wait until a given deadline. */
void sched_block_waiting_until(struct thread *, clock_t until);

/** Unblock all waiting threads whose deadline is inferior to \c deadline. */
void sched_unblock_waiting_before(clock_t deadline);

typedef struct {
    const bool old_if;
    bool done;
} sched_scope_t;

static inline sched_scope_t sched_scope_constructor(void)
{
    return (sched_scope_t){
        .old_if = scheduler_preempt_disable(),
        .done = false,
    };
}

static inline void sched_scope_destructor(sched_scope_t *scope)
{
    scheduler_preempt_enable(scope->old_if);
}

/** Define a scope during which the current thread should never be pre-empted.
 *
 *  WARNING: As this macro uses a for loop to function, any 'break' directive
 *  placed inside it will break out of the guarded scope instead of that of its
 *  containing loop.
 */
#define no_preemption_scope()                                  \
    for (sched_scope_t scope CLEANUP(sched_scope_destructor) = \
             sched_scope_constructor();                        \
         !scope.done; scope.done = true)
