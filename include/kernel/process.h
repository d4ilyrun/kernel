#pragma once

/**
 * @brief Process management
 * @file kernel/process.h
 *
 * @defgroup process Processes
 * @ingroup scheduling
 *
 * @{
 */

#include <kernel/types.h>
#include <kernel/vmm.h>

#include <libalgo/linked_list.h>
#include <utils/compiler.h>

#if ARCH == i686
#include <kernel/arch/i686/process.h>
#else
#error Unsupported arcihtecture
#endif

/** The max length of a process's name */
#define PROCESS_NAME_MAX_LEN 32

/** @enum process_state
 *  The different states a process can be in
 */
typedef enum process_state {
    SCHED_RUNNING = 0x0, ///< Currently running (or ready to run)
    SCHED_WAITING = 0x1, ///< Currently waiting for a resource (timer, lock ...)
    SCHED_KILLED = 0x2,  ///< A process has been killed, waiting for its
                         ///< resources to be disposed of
} process_state_t;

/**
 * @brief A single process.
 * @struct process
 */
typedef struct process {

    /**
     * Arch specific process context
     *
     * This includes registers, and information that must be kept for when
     * switching back into the process.
     */
    process_context_t context;
    process_state_t state; /*!< Process's current state */

    char name[PROCESS_NAME_MAX_LEN]; /*!< The process's name */
    pid_t pid;                       /*!< The PID */
    u32 flags; /*!< Combination of \ref process_flags values */

    vmm_t *vmm; /*!< The process's address space manager */

    node_t this; /*!< Intrusive linked list used by the scheduler */

    /** Information relative to the current state of the process */
    union {

        struct {
            /** End of the currently running process's timeslice */
            timestamp_t preempt;
        } running;

        /** For sleeping processes only */
        struct {
            timestamp_t wakeup; /*!< Time when it should wakeup (in ticks) */
        } sleep;
    };

} process_t;

/** @enum process_flags */
typedef enum process_flags {
    PROC_NONE,   ///< Default user mode process
    PROC_KERNEL, ///< This process runs in kernel mode
} process_flags;

static ALWAYS_INLINE bool process_is_kernel(process_t *process)
{
    return process->flags & PROC_KERNEL;
}

/** Process used when starting up the kernel.
 *
 * It is necesary to define it statically, since memory management functions are
 * not yet set up when first starting up.
 *
 * We should not reference this process anymore once we have an up and running
 * scheduler.
 */
extern process_t kernel_startup_process;

/** The currently running process */
extern process_t *current_process;

/** A function used as an entry point when starting a new process
 *
 * @param data Generic data passed on to the function when starting
 * the process
 * @note We never return from this function
 */
typedef void (*process_entry_t)(void *data);

/** Switch the currently running process
 * @param process The new process to switch into
 * @return \c false if the new process was previously killed
 */
bool process_switch(process_t *process);

/** Create and initialize a new process
 *
 * When starting a userland process, the \c data field is not passed
 * to the entrypoint function.
 *
 * @param name The name of the process
 * @param entrypoint The function called when starting the process
 * @param data Data passed to the entry function (can be NULL)
 * @param flags Feature flags: a combination of \ref process_flags enum values
 */
process_t *process_create(char *name, process_entry_t entrypoint, void *, u32);

/** Effectively kill a process
 *
 *  * Free all private memory used by the process
 *  * Synchronize resources (write to files, ...)
 */
void process_kill(process_t *);

/** @defgroup arch_process Processes - arch specifics
 *  @ingroup x86_process
 *
 *  @{
 */

/** @} */
