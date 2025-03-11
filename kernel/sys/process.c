#define LOG_DOMAIN "process"

#include <kernel/error.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/mmu.h>
#include <kernel/pmm.h>
#include <kernel/process.h>
#include <kernel/sched.h>

#include <string.h>

/** Minimum PID, should be given to the very first started thread */
#define PROCESS_FIRST_PID 1

// PID 0 is attributed to the IDLE task (and kernel startup but it is temporary)
static pid_t g_highest_pid = 0;

thread_t kernel_startup_process = {
    .name = "kstartup",
    .flags = THREAD_KERNEL,
    .tid = 0,
};

thread_t *current = &kernel_startup_process;

/** Arch specific, hardware level thread switching
 *
 * This updates the content of the registers to effectively switch
 * into the desired thread.
 *
 * @param context The next thread's hardware context
 */
void arch_thread_switch(thread_context_t *);

/** Arch specific, initialize the thread's arch specific context
 *
 * @param thread Pointer to thread to initialize
 * @param entrypoint The entrypoint used for starting this thread
 * @param data Data passed to the entry function (can be NULL)
 *
 * @return Whether we succeded or not
 */
bool arch_thread_create(thread_t *, thread_entry_t, void *);

/*
 * To create a thread we need to:
 *
 * * Initialize a new address space (VMM)
 * * Initialize a kernel stack
 * * Create a new page directory
 * * Copy the kernel's page table
 */
thread_t *
thread_create(char *name, thread_entry_t entrypoint, void *data, u32 flags)
{
    thread_t *new = kcalloc(1, sizeof(*new), KMALLOC_KERNEL);
    if (new == NULL) {
        log_err("Failed to allocate new thread");
        return NULL;
    }

    new->flags = flags;

    // The VMM cannot be initialized from within another thread's address space
    // This thread should be done by the arch specific wrapper responsible for
    // first starting up the thread.
    vmm_t *vmm = kmalloc(sizeof(*vmm), KMALLOC_KERNEL);
    if (vmm == NULL) {
        log_err("Failed to allocate VMM");
        kfree(new);
        return NULL;
    }

    strncpy(new->name, name, PROCESS_NAME_MAX_LEN);
    new->tid = g_highest_pid++;
    new->vmm = vmm;

    if (!arch_thread_create(new, entrypoint, data)) {
        kfree(vmm);
        kfree(new);
    }

    return new;
}

void arch_thread_free(thread_t *thread);

MAYBE_UNUSED static void thread_free(thread_t *thread)
{
    log_info("terminating '%s'", thread->name);
    vmm_destroy(thread->vmm);
    arch_thread_free(thread);
    kfree(thread);
}

bool thread_switch(thread_t *thread)
{
    if (thread->state == SCHED_KILLED) {
        // FIXME: Find a way to free the thread on exit
        //        By doing it this way, referencing the freed thread
        //        causes a #PF when rescheduling the next thread.
        //
        // thread_free(thread);
        return false;
    }

    arch_thread_switch(&thread->context);
    return true;
}

void thread_kill(thread_t *thread)
{
    thread->state = SCHED_KILLED;
    if (thread == current)
        schedule();
}

NO_RETURN void
arch_thread_jump_to_userland(thread_entry_t entrypoint, void *data);

NO_RETURN void thread_jump_to_userland(thread_entry_t entrypoint, void *data)
{
    arch_thread_jump_to_userland(entrypoint, data);
}
