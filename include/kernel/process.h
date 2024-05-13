#pragma once

/**
 * @brief Scheduler implementation
 *
 * @defgroup scheduling Scheduling
 * @ingroup kernel
 *
 * @{
 */

#include <kernel/types.h>
#include <kernel/vmm.h>

#if ARCH == i686
#include <kernel/arch/i686/task.h>
#else
#error Unsupported arcihtecture
#endif

/** The max length of a process's name */
#define PROCESS_NAME_MAX_LEN 32

/**
 * A single process.
 * @struct process
 */
typedef struct process {

    char name[PROCESS_NAME_MAX_LEN]; ///< The process's name
    pid_t pid;                       ///< The PID

    /**
     * Arch specific task context
     *
     * This includes registers, and information that must be kept for when
     * switching back into the task.
     */
    task_t task; ///< Arch specific hardware context

    vmm_t *vmm; ///< The process's address space manager

} process_t;

/** Process used when starting up the kernel.
 *
 * It is necesary to define it statically, since memory management functions are
 * not yet set up when first starting up.
 *
 * We should not reference this process anymore once we have an up and running
 * scheduler.
 */
extern process_t kernel_startup_process;
