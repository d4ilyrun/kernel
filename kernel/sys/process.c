#include <kernel/error.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/mmu.h>
#include <kernel/pmm.h>
#include <kernel/process.h>

#include <string.h>

/** Minimum PID, should be given to the very first started process */
#define PROCESS_FIRST_PID 1

static pid_t g_highest_pid = PROCESS_FIRST_PID;

process_t kernel_startup_process = {
    .name = "kstartup",
    .pid = 0,
};

process_t *current_process = &kernel_startup_process;

/*
 * To create a process we need to:
 *
 * * Initialize a new address space (VMM)
 * * Initialize a kernel stack
 * * Create a new page directory
 * * Copy the kernel's page table
 */
process_t *process_create(char *name, process_entry_t entrypoint)
{
    process_t *new = kmalloc(sizeof(*new), KMALLOC_KERNEL);
    if (new == NULL) {
        log_err("SCHED", "Failed to allocate new process");
        return NULL;
    }

    // The VMM cannot be initialized from within another process's address space
    // This process should be done by the arch specific wrapper responsible for
    // first starting up the process.
    vmm_t *vmm = kmalloc(sizeof(*vmm), KMALLOC_KERNEL);
    if (vmm == NULL) {
        log_err("SCHED", "Failed to allocate VMM");
        kfree(new);
        return NULL;
    }

    strncpy(new->name, name, PROCESS_NAME_MAX_LEN);
    new->pid = g_highest_pid++;
    new->vmm = vmm;

    if (!arch_process_create(new, entrypoint)) {
        // TODO: vmm_release
        kfree(vmm);
        kfree(new);
    }

    return new;
}

void process_switch(process_t *process)
{
    arch_process_switch(&process->context);
}
