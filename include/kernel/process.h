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
#include <kernel/arch/i686/process.h>
#else
#error Unsupported arcihtecture
#endif

/** The max length of a process's name */
#define PROCESS_NAME_MAX_LEN 32

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

    char name[PROCESS_NAME_MAX_LEN]; /*!< The process's name */
    pid_t pid;                       /*!< The PID */

    vmm_t *vmm; /*!< The process's address space manager */

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

/** The currently running process */
extern process_t *current_process;

/** A function used as an entry point when starting a new process
 *
 * @param data Generic data passed on to the function when starting the process
 * @note We never return from this function
 */
typedef void (*process_entry_t)(void *data);

/** Switch the currently running process
 * @param process The new process to switch into
 */
void process_switch(process_t *process);

/** Create and initialize a new process
 *
 * @param name The name of the process
 * @param entrypoint The function called when starting the process
 */
process_t *process_create(char *name, process_entry_t entrypoint);

/** @defgroup arch_process Processes - arch specifics
 *  @ingroup x86_process
 *
 *  @{
 */

/** Arch specific, hardware level process switching
 *
 * This updates the content of the registers to effectively switch into the
 * desired process.
 *
 * @param context The next process's hardware context
 *
 * @warning Do not call this function directly! This should only be
 *          called by @ref process_switch_into
 */
void arch_process_switch(process_context_t *);

/** Arch specific, initialize the process's arch specific context
 *
 * @param process Pointer to process to initialize
 * @param entrypoint The entrypoint used for starting this process
 *
 * @return Whether we succeded or not
 *
 * @warning Do not call this function directly! This should only be
 *          called by @ref process_create
 */
bool arch_process_create(process_t *process, process_entry_t entrypoint);

/** @} */
