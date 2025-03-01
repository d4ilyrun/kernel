#define LOG_DOMAIN "process"

#include <kernel/error.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/mmu.h>
#include <kernel/pmm.h>
#include <kernel/process.h>
#include <kernel/sched.h>

#include <string.h>

/** Minimum PID, should be given to the very first started process */
#define PROCESS_FIRST_PID 1

// PID 0 is attributed to the IDLE task (and kernel startup but it is temporary)
static pid_t g_highest_pid = 0;

process_t kernel_startup_process = {
    .name = "kstartup",
    .flags = PROC_KERNEL,
    .pid = 0,
};

process_t *current_process = &kernel_startup_process;

/** Arch specific, hardware level process switching
 *
 * This updates the content of the registers to effectively switch
 * into the desired process.
 *
 * @param context The next process's hardware context
 */
void arch_process_switch(process_context_t *);

/** Arch specific, initialize the process's arch specific context
 *
 * @param process Pointer to process to initialize
 * @param entrypoint The entrypoint used for starting this process
 * @param data Data passed to the entry function (can be NULL)
 *
 * @return Whether we succeded or not
 */
bool arch_process_create(process_t *process, process_entry_t entrypoint,
                         void *);

/*
 * To create a process we need to:
 *
 * * Initialize a new address space (VMM)
 * * Initialize a kernel stack
 * * Create a new page directory
 * * Copy the kernel's page table
 */
process_t *
process_create(char *name, process_entry_t entrypoint, void *data, u32 flags)
{
    process_t *new = kcalloc(1, sizeof(*new), KMALLOC_KERNEL);
    if (new == NULL) {
        log_err("Failed to allocate new process");
        return NULL;
    }

    new->flags = flags;

    // The VMM cannot be initialized from within another process's address space
    // This process should be done by the arch specific wrapper responsible for
    // first starting up the process.
    vmm_t *vmm = kmalloc(sizeof(*vmm), KMALLOC_KERNEL);
    if (vmm == NULL) {
        log_err("Failed to allocate VMM");
        kfree(new);
        return NULL;
    }

    strncpy(new->name, name, PROCESS_NAME_MAX_LEN);
    new->pid = g_highest_pid++;
    new->vmm = vmm;

    if (!arch_process_create(new, entrypoint, data)) {
        kfree(vmm);
        kfree(new);
    }

    return new;
}

void arch_process_free(process_t *process);

MAYBE_UNUSED static void process_free(process_t *process)
{
    log_info("terminating '%s'", process->name);
    vmm_destroy(process->vmm);
    arch_process_free(process);
    kfree(process);
}

bool process_switch(process_t *process)
{
    if (process->state == SCHED_KILLED) {
        // FIXME: Find a way to free the process on exit
        //        By doing it this way, referencing the freed process
        //        causes a #PF when rescheduling the next process.
        //
        // process_free(process);
        return false;
    }

    arch_process_switch(&process->context);
    return true;
}

void process_kill(process_t *process)
{
    process->state = SCHED_KILLED;
    if (process == current_process)
        schedule();
}

NO_RETURN void
arch_process_jump_to_userland(process_entry_t entrypoint, void *data);

NO_RETURN void process_jump_to_userland(process_entry_t entrypoint, void *data)
{
    arch_process_jump_to_userland(entrypoint, data);
}
