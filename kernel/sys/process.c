#define LOG_DOMAIN "process"

#include <kernel/error.h>
#include <kernel/interrupts.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/mmu.h>
#include <kernel/pmm.h>
#include <kernel/process.h>
#include <kernel/sched.h>

#include <libalgo/linked_list.h>
#include <utils/container_of.h>

#include <string.h>

/** Reserved PID for the kernel process */
#define PROCESS_KERNEL_PID 0

/** Minimum PID, should be given to the very first started thread */
#define PROCESS_FIRST_PID 1

/*
 * Global pool of PIDs.
 * NOTE: PIDs and TIDs use the same pool.
 */
static pid_t g_highest_pid = PROCESS_FIRST_PID;

struct thread kernel_process_initial_thread = {
    .process = &kernel_process,
    .flags = THREAD_KERNEL,
    .tid = PROCESS_KERNEL_PID,
};

struct process kernel_process = {
    .name = "kstartup",
    .threads = &kernel_process_initial_thread.proc_this,
    .refcount = 1, /* static initial thread */
    .pid = PROCESS_KERNEL_PID,
};

thread_t *current = &kernel_process_initial_thread;

/** Arch specific, hardware level thread switching
 *
 * This updates the content of the registers to effectively switch
 * into the desired thread.
 *
 * @param context The next thread's hardware context
 */
extern void arch_thread_switch(thread_context_t *);

/** Arch specific, initialize the thread's arch specific context
 *
 * @param thread Pointer to thread to initialize
 * @param entrypoint The entrypoint used for starting this thread
 * @param data Data passed to the entry function (can be NULL)
 *
 * @return Whether we succeded or not
 */
extern bool arch_thread_init(thread_t *, thread_entry_t, void *);

extern void arch_process_free(struct process *);

extern void arch_thread_free(thread_t *thread);

NO_RETURN void
arch_thread_jump_to_userland(thread_entry_t entrypoint, void *data);

extern void arch_thread_set_mmu(struct thread *thread, paddr_t mmu);

/** Free all resources currently held by a thread.
 *
 * This should never be called directly. Instead, it should be
 * automatically called when the process' reference count reaches 0.
 *
 * @see process_get process_put
 */
static void process_free(struct process *process)
{
    log_info("freeing process: %s", process->name);

    arch_process_free(process);

    address_space_clear(process->as);
    address_space_load(&kernel_address_space);
    address_space_destroy(process->as);

    kfree(process);
}

static struct process *process_get(struct process *process)
{
    if (!process)
        return NULL;

    process->refcount += 1;
    return process;
}

static struct process *process_put(struct process *process)
{
    if (!process)
        return NULL;

    process->refcount -= 1;

    if (process->refcount == 0) {
        process_free(process);
        return NULL;
    }

    return process;
}

struct process *
process_create(const char *name, thread_entry_t entrypoint, void *data)
{
    struct process *process = NULL;
    struct thread *initial_thread = NULL;

    process = kcalloc(1, sizeof(*process), KMALLOC_KERNEL);
    if (process == NULL)
        return PTR_ERR(E_NOMEM);

    process->as = address_space_new();
    if (IS_ERR(process->as)) {
        log_err("failed to create process %s: %s", name,
                err_to_str(ERR_FROM_PTR(process->as)));
        kfree(process);
        return (void *)process->as;
    }

    strncpy(process->name, name, PROCESS_NAME_MAX_LEN);

    // The initial execution thread is created along with the process
    initial_thread = thread_spawn(process, entrypoint, data, THREAD_KERNEL);
    if (initial_thread == NULL)
        goto process_destroy;

    llist_add(&process->threads, &initial_thread->proc_this);

    return process;

process_destroy:
    kfree(initial_thread);
    address_space_destroy(process->as);
    kfree(process);
    return PTR_ERR(E_NOMEM);
}

void process_kill(struct process *process)
{
    if (process == &kernel_process) {
        log_err("Trying to free the kernel process");
        return;
    }

    // Avoid race condition where the current thread would be rescheduled after
    // being marked killable, and before having marked the rest of the threads
    no_preemption_scope () {
        FOREACH_LLIST (node, process->threads) {
            struct thread *thread = container_of(node, struct thread,
                                                 proc_this);
            thread->state = SCHED_KILLED;
        }
    }

    if (current->process == process)
        schedule_preempt();
}

void process_init_kernel_process(void)
{
    error_t err;

    /*
     * Userspace address space is inherited by processes when forking.
     *
     * This being the only way of creating new processes, it means that
     * we have to create an 'initial' user address space for the kernel
     * process that will be inherited by userland processes created by
     * the kernel directly (e.g. init).
     *
     * The kernel should never ever write to this userspace address space.
     */

    kernel_process.as = address_space_new();
    if (IS_ERR(kernel_process.as))
        PANIC("Failed to create kernel user address space: %s",
              err_to_str(ERR_FROM_PTR(kernel_process.as)));

    /*
     * We need the new kernel mmu to be loaded before initializing the
     * VMM's content, or else the reserved area will be placed inside the
     * kernel-only address space.
     */
    if (address_space_load(kernel_process.as))
        PANIC("Failed to load kernel user address space");

    err = address_space_init(kernel_process.as);
    if (err != E_SUCCESS)
        PANIC("Failed to initialize kernel user address space: %s",
              err_to_str(err));
}

thread_t *thread_spawn(struct process *process, thread_entry_t entrypoint,
                       void *data, u32 flags)
{
    thread_t *thread;

    /* Userland processes cannot spawn kernel threads */
    if (flags & THREAD_KERNEL && process != &kernel_process)
        return NULL;

    thread = kcalloc(1, sizeof(*thread), KMALLOC_KERNEL);
    if (thread == NULL) {
        log_err("Failed to allocate new thread");
        return NULL;
    }

    thread->flags = flags;
    thread->process = process_get(process);

    /* The initial thread's TID is equal to its containing process's PID */
    if (llist_is_empty(process->threads))
        thread->tid = process->pid;
    else
        thread->tid = g_highest_pid++;

    if (!arch_thread_init(thread, entrypoint, data)) {
        log_err("Failed to initialize new thread");
        kfree(thread);
        return NULL;
    }

    return thread;
}

MAYBE_UNUSED static void thread_free(thread_t *thread)
{
    const bool interrupts = scheduler_preempt_disable();
    struct process *process = thread->process;

    log_info("terminating thread %d (%s)", thread->tid, process->name);

    llist_remove(&process->threads, &thread->proc_this);

    // Actually free the thread.
    arch_thread_free(thread);
    kfree(thread);

    /*
     * Release reference this threads holds onto the process.
     * This will also free the process if this is the process' last
     * running thread.
     */
    process_put(process);

    scheduler_preempt_enable(interrupts);
}

bool thread_switch(thread_t *thread)
{
    if (thread->state == SCHED_KILLED) {
        thread_free(thread);
        return false;
    }

    arch_thread_switch(&thread->context);
    return true;
}

void thread_kill(thread_t *thread)
{
    /* To make the implementation easier the actual 'killing' of the thread
     * is delayed until it is rescheduled (cf. thread_switch). */
    thread->state = SCHED_KILLED;
    if (thread == current)
        schedule_preempt();
}

NO_RETURN void thread_jump_to_userland(thread_entry_t entrypoint, void *data)
{
    arch_thread_jump_to_userland(entrypoint, data);
}

void thread_set_mmu(struct thread *thread, paddr_t mmu)
{
    arch_thread_set_mmu(thread, mmu);
}
