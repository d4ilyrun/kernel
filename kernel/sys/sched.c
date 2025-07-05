#include <kernel/atomic.h>
#include <kernel/cpu.h>
#include <kernel/devices/timer.h>
#include <kernel/init.h>
#include <kernel/interrupts.h>
#include <kernel/sched.h>
#include <kernel/spinlock.h>

#include <libalgo/queue.h>
#include <utils/constants.h>
#include <utils/container_of.h>

bool scheduler_initialized = false;

/** The maximum timeslice given to a thread by the scheduler */
#define SCHED_TIMESLICE MS_TO_TICKS(2ULL) // 2MS

typedef struct scheduler {

    /** The runqueue
     * All threads inside this queue are ready to run and could theoretically
     * be switched to at any moment.
     */
    queue_t ready;

    /** Fields used for synchronization in a multiprocessor environment */
    struct {
        atomic_t preemption_level;
    } sync;

} scheduler_t;

static scheduler_t scheduler;

/** IDLE task running when there are no other task ready */
static thread_t *idle_thread;

/** @brief Reschedule the current thread (multiprocessor-unsafe)
 *
 * This is the actual implementation for @ref schedule.
 * It is meant to be called when already holding a lock onto the scheduler.
 *
 * @warning BE SURE TO LOCK THE SCHEDULER WHEN CALLING THIS FUNCTION
 *
 * @see scheduler_lock scheduler_unlock
 */
static void schedule_locked(bool preempt, bool reschedule)
{
    node_t *next_node;

    if (atomic_read(&scheduler.sync.preemption_level) > 1 && !preempt)
        return;

    next_node = queue_dequeue(&scheduler.ready);
    if (next_node == NULL)
        return;

    thread_t *next = container_of(next_node, thread_t, this);

    if (reschedule) {
        if (current->state != SCHED_WAITING)
            queue_enqueue(&scheduler.ready, &current->this);
    }

    // If some tasks are ready, do not reschedule the idle task
    if (next == idle_thread && !queue_is_empty(&scheduler.ready)) {
        next_node = queue_dequeue(&scheduler.ready);
        next = container_of(next_node, thread_t, this);
        queue_enqueue(&scheduler.ready, &idle_thread->this);
    }

    next->running.preempt = timer_gettick() + SCHED_TIMESLICE;

    if (!thread_switch(next)) {
        schedule_locked(preempt, false);
    }
}

void schedule(void)
{
    const bool old_if = scheduler_preempt_disable();
    schedule_locked(false, true);
    scheduler_preempt_enable(old_if);
}

void schedule_preempt(void)
{
    const bool old_if = scheduler_preempt_disable();
    schedule_locked(true, true);
    scheduler_preempt_enable(old_if);
}

bool scheduler_preempt_disable(void)
{
    bool if_flag = interrupts_test_and_disable();
    atomic_inc(&scheduler.sync.preemption_level);
    return if_flag;
}

void scheduler_preempt_enable(bool old_if_flag)
{
    if (atomic_read(&scheduler.sync.preemption_level))
        atomic_dec(&scheduler.sync.preemption_level);

    // Re-enable interrupts **only** if they were enabled prior to locking
    if (old_if_flag)
        interrupts_enable();
}

static void idle_task(void *data __attribute__((unused)))
{
    while (1) {
        interrupts_enable();
        hlt();
    }
}

void sched_new_thread(thread_t *thread)
{
    if (thread == NULL)
        return;

    thread->state = SCHED_RUNNING;
    queue_enqueue(&scheduler.ready, &thread->this);
}

void sched_block_thread(struct thread *thread)
{
    const bool old_if = scheduler_preempt_disable();

    if (thread->state != SCHED_RUNNING)
        goto block_thread_exit;

    thread->state = SCHED_WAITING;
    if (thread == current)
        schedule_locked(true, true);

block_thread_exit:
    scheduler_preempt_enable(old_if);
}

void sched_unblock_thread(thread_t *thread)
{
    const bool old_if = scheduler_preempt_disable();

    // Avoid resurecting a thread that had been killed in the meantime
    if (thread->state == SCHED_WAITING)
        thread->state = SCHED_RUNNING;

    queue_enqueue(&scheduler.ready, &thread->this);

    // give the least time possible to the IDLE task
    if (current == idle_thread)
        schedule_locked(true, true);

    scheduler_preempt_enable(old_if);
}

static error_t scheduler_init(void)
{
    atomic_write(&scheduler.sync.preemption_level, 0);
    INIT_QUEUE(scheduler.ready);

    idle_thread = thread_spawn(&kernel_process, idle_task, NULL, NULL,
                               THREAD_KERNEL);
    // use the largest PID possible to avoid any conflict later on
    idle_thread->tid = (pid_t)-1;
    sched_new_thread(idle_thread);
    scheduler_initialized = true;

    return E_SUCCESS;
}

DECLARE_INITCALL(INIT_LATE, scheduler_init);
