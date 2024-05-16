#pragma once

/**
 * @brief Scheduler implementation
 *
 * @defgroup scheduling Scheduling
 * @ingroup kernel
 *
 * # Scheduler
 *
 * The scheduler is responsible for ... well, scheduling processes.
 *
 * It has to decide which process to run next, and when, among all currently
 * running processes. This is what allows running multiple processes on a single
 * CPU.
 *
 * A good scheduler is crucial to the user's experience, and this is often what
 * makes the "feel" of the entire OS.
 *
 * ## Design
 *
 * For now, our scheduler uses a preemptive round-robin design.
 * It holds a list of currently running processes, called the runqueue,
 * and cycles between them at a regular interval.
 *
 * The interval is set currently set to 2MS, per process, per cycle, and is
 * handled inside \ref irq_timer_handler. If the current process is still
 * running when the current interval reaches its end, the next one takes its
 * place, and the timer is reset. This is called preemption.
 *
 * During the execution of a processes, it often needs to access some
 * resources, thus having to wait until the resource is available. When this is
 * the case, the process is marked as \ref SCHED_WAITING using \ref
 * sched_block_current_process, and we switch to the next available running
 * process. Once the resource is available, the relevant interface has the
 * resposibility to notify the scheduler that the process can be rescheduled,
 * using \ref sched_unblock_process.
 *
 * ## Improvements
 *
 * * Use a separate timer for pre-emption, instead of mingling with the one used
 *   for timekeeping. This would allow for dynamically resetting the timer
 *   interrupt without sacrificing precision on ticks.
 *
 * * Priority levels. We would like to give more time to more "important"
 *   processes ideally. This would require multiple runqueues, but is a MUST
 *   have for any actual scheduler.
 *
 * * SMP: when activating multiprocessing, we want to setup a scheduler per
 *   core, and dispatch processes across them. This would also imply other smart
 *   opimizations (time stealing, ...)
 *
 * @{
 */

#include <kernel/process.h>

// TODO: Remove this once we implemented another timer for the scheduler
extern bool scheduler_initialized;

/**
 * @brief Reschedule the current process
 *
 * This is the **main** function of the scheduler. It is called when we want to
 * switch to the next scheduled process. It automatically reinserts the current
 * process into the correct queue depending on its state.
 */
void schedule(void);

/** Lock the different interna synchronization mechanisms
 *  @return Wether interrupts were previously enabled
 */
bool scheduler_lock(void);

/** Release the different locks and synchorinzation mechanisms
 *  @param old_if_flag The state of the interrputs prior to locking
 */
void scheduler_unlock(bool old_if_flag);

/** Add a new process to be scheduled.
 *  When adding a new process, its state will be set to @ref SCHED_RUNNING
 */
void sched_new_process(process_t *);

/** Initialize this cpu's scheduler */
void scheduler_init(void);

/** Mark the current process as blocked
 *
 * A blocked process cannot be rescheduled until it is explicitely marked as
 * @ref SCHED_RUNNING.
 */
void sched_block_current_process(void);

/** Unblock a currently blocked process
 *
 * The process is marked as @ref SCHED_RUNNING and is automatically added to the
 * appropriate runqueue.
 */
void sched_unblock_process(process_t *process);
