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

/** @enum thread_state
 *  The different states a thread can be in
 */
typedef enum thread_state {
    SCHED_RUNNING = 0x0, ///< Currently running (or ready to run)
    SCHED_WAITING = 0x1, ///< Currently waiting for a resource (timer, lock ...)
    SCHED_KILLED = 0x2,  ///< A thread has been killed, waiting for its
                         ///< resources to be disposed of
} thread_state_t;

typedef llist_t kmalloc_t;

/**
 * @brief A single thread.
 * @struct thread
 */
typedef struct thread {

    /**
     * Arch specific thread context
     *
     * This includes registers, and information that must be kept for when
     * switching back into the thread.
     */
    thread_context_t context;
    thread_state_t state; /*!< thread's current state */

    char name[PROCESS_NAME_MAX_LEN]; /*!< The thread's name */
    pid_t tid;                       /*!< Thread ID */
    u32 flags; /*!< Combination of \ref thread_flags values */

    vmm_t *vmm;        /*!< The thread's address space manager */
    kmalloc_t kmalloc; /*!< Opaque struct used by the memory allocator to
                          allocate memory blocks inside the user area */

    node_t this; /*!< Intrusive linked list used by the scheduler */

    /** Information relative to the current state of the thread */
    union {

        struct {
            /** End of the currently running thread's timeslice */
            timestamp_t preempt;
        } running;
        /** For sleeping threads only */
        struct {
            timestamp_t wakeup; /*!< Time when it should wakeup (in ticks) */
        } sleep;
    };

} thread_t;

/** @enum thread_flags */
typedef enum thread_flags {
    THREAD_KERNEL = BIT(0), ///< This thread runs in kernel mode
} process_flags_t;

/***/
static ALWAYS_INLINE bool thread_is_kernel(thread_t *thread)
{
    return thread->flags & THREAD_KERNEL;
}

/** Process used when starting up the kernel.
 *
 * It is necesary to define it statically, since memory management functions are
 * not yet set up when first starting up.
 *
 * We should not reference this process anymore once we have an up and running
 * scheduler.
 */
extern thread_t kernel_startup_process;

/** The currently running thread */
extern thread_t *current;

/** A function used as an entry point when starting a new thread
 *
 * @param data Generic data passed on to the function when starting
 * the thread
 * @note We never return from this function
 */
typedef void (*thread_entry_t)(void *data);

/** Switch the currently running thread
 * @param process The new thread to switch into
 * @return \c false if the new thread was previously killed
 */
bool thread_switch(thread_t *);

/** Create and initialize a new thread
 *
 * When starting a userland thread, the \c data field is not passed
 * to the entrypoint function.
 *
 * @param name The name of the thread
 * @param entrypoint The function called when starting the thread
 * @param data Data passed to the entry function (can be NULL)
 * @param flags Feature flags: a combination of \ref thread_flags enum values
 */
thread_t *thread_create(char *name, thread_entry_t entrypoint, void *, u32);

/** Start executing code in userland
 *
 * This function resets the user execution context (eip, stack content).
 * It then changes the privilege level to be that of userland, and jumps
 * onto the given address.
 *
 * @info This serves as a temporary equivalent to the execve syscall for testing
 *       purposes.
 *
 * @param entrypoint The entrypoint address to jump onto
 * @param data       The data passed as an argument to the 'entrypoint' function
 *                   (ignored)
 */
NO_RETURN void thread_jump_to_userland(thread_entry_t, void *);

/** Effectively kill a thread
 *
 *  * Free all private memory used by the thread
 *  * Synchronize resources (write to files, ...)
 */
void thread_kill(thread_t *);

/** @defgroup arch_process Processes - arch specifics
 *  @ingroup x86_process
 *
 *  @{
 */

/** @} */
