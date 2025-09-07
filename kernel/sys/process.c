/*
 * Process & threads implementation.
 *
 * ## Threads
 *
 * Threads can be created in 2 ways:
 *
 * - Forking creates the initial thread along with the new process. This thread
 *   will always be a user one.
 *
 * - thread_spawn() is the lower level function, responsible for creating
 *   inividual threads. It is called by thread_fork(), but can also be called
 *   manually by the kernel to create kernel threads.
 *
 * User threads can only be killed manually by the kernel, or when killing the
 * containing process. The kernel kills kernell threads through thread_kill().
 *
 * Each thread is allocated a personal kernel stack during creation, which will
 * be used when running in kernel context. User threads inherit their user stack
 * from the forked process (through the CoW mechanism). Kernel threads never
 * run in userland and do not require a userland stack, so they share a common
 * user-stack, allocated at startup and inherited by the init process (which
 * is forked from the initial kernel process).
 *
 * ## Processes
 *
 * Processes can only be created by forking the current running process.
 * This is also the case for the init process, which is forked from the initial
 * kernel process (which is provided with a dummy user-stack for this purpose).
 *
 * Processes can only be killed in 2 ways:
 *
 *  - process_kill() is called when the process calls the exit() syscall.
 *
 *  - By the kernel when the process raises an exception or receives a signal
 *    whose default action is to kill the process (TODO).
 *
 * ### Kernel process
 *
 * There exists a single kernel process (called ... kernel_process), declared
 * as a global variable. This process is the parent to all kernel threads. It
 * comes with its own address space, from which the mappings are shared with
 * every other processes in the system to be used when jumping in kernel mode.
 *
 * ### Delay
 *
 * In both cases, the actual killing of the process is delayed until all its
 * threads have been removed. The process_kill() function simply mark
 * all of the process's threads as SCHED_KILLED, so that they are removed and
 * freed the next time they are scheduled (see thread_switch()).
 *
 * All threads hold a reference to their process. Once the reference count
 * reaches 0 the process is released and made a zombie, waiting for its parent
 * to collect it (by calling wait(), see process_collect_zombie()).
 */

#define LOG_DOMAIN "process"

#include <kernel/error.h>
#include <kernel/execfmt.h>
#include <kernel/file.h>
#include <kernel/interrupts.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/mmu.h>
#include <kernel/pmm.h>
#include <kernel/process.h>
#include <kernel/sched.h>
#include <kernel/spinlock.h>
#include <kernel/syscalls.h>
#include <kernel/vfs.h>

#include <libalgo/linked_list.h>
#include <utils/container_of.h>

#include <limits.h>
#include <sys/wait.h>

/** Reserved PID for the kernel process */
#define PROCESS_KERNEL_PID 0

/**
 * Minimum PID, should be given to the very first started thread.
 * PID number 1 is reserved for the init process.
 */
#define PROCESS_FIRST_PID 2

struct thread kernel_process_initial_thread = {
    .process = &kernel_process,
    .flags = THREAD_KERNEL,
    .tid = PROCESS_KERNEL_PID,
};

struct process kernel_process = {
    .name = "kstartup",
    .refcount = 1, /* static initial thread */
    .pid = PROCESS_KERNEL_PID,
};

thread_t *current = &kernel_process_initial_thread;

struct process *init_process = NULL;

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
 * @param esp The value to put inside the stack pointer before starting
 *
 * @return Whether we succeded or not
 */
extern error_t
arch_thread_init(thread_t *, thread_entry_t, void *data, void *esp);

extern void arch_process_clear(struct process *);

extern void arch_thread_clear(thread_t *thread);

NO_RETURN void
arch_thread_jump_to_userland(thread_entry_t entrypoint, void *data);

extern void arch_thread_set_mmu(struct thread *thread, paddr_t mmu);

static void thread_free(thread_t *thread);
static void thread_kill_locked(thread_t *thread);

/**
 * @return the next available PID.
 *  NOTE: PIDs and TIDs use the same pool.
 */
static pid_t process_next_pid(void)
{
    static pid_t g_highest_pid = PROCESS_FIRST_PID;
    static DECLARE_SPINLOCK(pid_lock);

    pid_t pid;

    locked_scope (&pid_lock) {
        pid = g_highest_pid;
        if (__builtin_add_overflow(g_highest_pid, 1, &g_highest_pid))
            log_err("!!! PID OVERFLOW !!!");
    }

    return pid;
}

/*
 * Collect and free a zombie child process.
 *
 * @return true if no other waiting thread remains.
 */
static void process_collect_zombie(struct process *zombie)
{
    if (WARN_ON(READ_ONCE(zombie->state) != SCHED_ZOMBIE))
        return;

    WRITE_ONCE(zombie->state, SCHED_KILLED);
    llist_remove(&zombie->this);

    kfree(zombie);
}

/**
 * Free all resources currently held by a process before it can be collected
 * by its parent.
 *
 * This is the core of the process_kill() function. It is called once
 * all of the process' threads have been killed. This should never be called
 * directly. Instead, it should be automatically called when the process'
 * reference count reaches 0.
 *
 * @see process_get() process_put()
 */
static void process_make_zombie(struct process *process)
{
    struct process *child;

    log_info("freeing process: %s", process->name);

    if (process == init_process)
        PANIC("Trying to kill the init process.");

    spinlock_acquire(&process->lock);

    WRITE_ONCE(process->state, SCHED_ZOMBIE);

    /*
     * Release all open files.
     */
    locked_scope (&process->files_lock) {
        for (size_t i = 0; i < PROCESS_FD_COUNT; ++i) {
            if (process->files[i])
                file_put(process->files[i]);
        }
    }

    address_space_destroy(process->as);

    /* Attach all orphans to the init process. */
    FOREACH_LLIST_ENTRY(child, &process->children, this)
    {
        locked_scope (&child->lock) {
            llist_add(&init_process->children, &child->this);
        }
    }

    spinlock_release(&process->lock);

    /*
     * TODO: a SIGCHLD shall be sent to the parent process.
     */
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
        process_make_zombie(process);
        return NULL;
    }

    return process;
}

static struct process *process_new(void)
{
    struct process *process = NULL;

    process = kcalloc(1, sizeof(*process), KMALLOC_KERNEL);
    if (process == NULL)
        return PTR_ERR(E_NOMEM);

    process->as = address_space_new();
    if (IS_ERR(process->as)) {
        log_err("failed to create process: %pE", process->as);
        kfree(process);
        return (void *)process->as;
    }

    process->pid = process_next_pid();
    process->state = SCHED_RUNNING;

    INIT_SPINLOCK(process->files_lock);
    INIT_SPINLOCK(process->lock);
    INIT_LLIST(process->threads);
    INIT_LLIST(process->children);

    return process;
}

void process_kill(struct process *process, int status)
{
    if (process == &kernel_process) {
        log_err("Trying to free the kernel process");
        stack_trace();
        return;
    }

    /*
     * In a multiprocessor environment, we could well have 2 threads (or more)
     * of a same process calling the _exit() syscall at the same time. This
     * would create a race condition where the second thread's exit status
     * would overwrite the first one.
     */
    locked_scope (&process->lock) {

        if (process->state == SCHED_KILLED)
            goto reschedule_current;

        /*
         * Avoid race condition where the current thread would be rescheduled
         * after being marked killable, and before having marked the rest
         * of the threads.
         */
        no_preemption_scope () {
            FOREACH_LLIST_SAFE (node, tmp, &process->threads) {
                struct thread *thread = container_of(node, struct thread,
                                                     proc_this);
                thread_kill_locked(thread);
            }
        }

        process->exit_status = status;
    }

reschedule_current:
    if (process == current->process)
        schedule_preempt();
}

void process_init_kernel_process(void)
{
    void *ustack = NULL;
    error_t err;

    INIT_LLIST(kernel_process.threads);
    INIT_LLIST(kernel_process.children);
    INIT_SPINLOCK(kernel_process.lock);
    INIT_SPINLOCK(kernel_process.files_lock);

    llist_add(&kernel_process.threads,
              &kernel_process_initial_thread.proc_this);

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
        PANIC("Failed to create kernel user address space: %pE",
              kernel_process.as);

    /*
     * We need the new kernel mmu to be loaded before initializing the
     * VMM's content, or else the reserved area will be placed inside the
     * kernel-only address space.
     */
    if (address_space_load(kernel_process.as))
        PANIC("Failed to load kernel user address space");

    err = address_space_init(kernel_process.as);
    if (err != E_SUCCESS)
        PANIC("Failed to initialize kernel user address space: %pe", &err);

    ustack = vm_alloc(kernel_process.as, USER_STACK_SIZE,
                      VM_READ | VM_WRITE | VM_CLEAR);
    if (ustack == NULL)
        PANIC("Fail to initialize initial user stack");

    /* Makes it easier for us to retrieve the kernel user stack when needed. */
    if (current != &kernel_process_initial_thread)
        PANIC("Kernel process initialization MUST come before creating "
              "any other thread");

    thread_set_user_stack(&kernel_process_initial_thread, ustack);
}

int process_register_file(struct process *process, struct file *file)
{
    int fd = -E_MFILE;

    /*
     * Find the first available file descriptor index.
     */
    locked_scope (&process->files_lock) {
        for (size_t i = 0; i < PROCESS_FD_COUNT; ++i) {
            if (process->files[i] == NULL)
                continue;
            fd = i;
            process->files[i] = file;
            break;
        }
    }

    return fd;
}

error_t process_unregister_file(struct process *process, int fd)
{
    struct file *file;

    if (fd >= PROCESS_FD_COUNT)
        return E_BAD_FD;

    locked_scope (&process->files_lock) {
        file = process->files[fd];
        if (!file)
            return E_BAD_FD;
        file_put(file);
        process->files[fd] = NULL;
    }

    return E_SUCCESS;
}

struct file *process_file_get(struct process *process, int fd)
{
    struct file *file;

    if (fd >= PROCESS_FD_COUNT)
        return NULL;

    locked_scope (&process->files_lock) {
        file = process->files[fd];
        if (file)
            file_get(file);

    }

    return file;
}

thread_t *thread_spawn(struct process *process, thread_entry_t entrypoint,
                       void *data, void *esp, u32 flags)
{
    thread_t *thread;
    void *kstack = NULL;
    error_t err;

    /* Userland processes cannot spawn kernel threads */
    if (flags & THREAD_KERNEL && process != &kernel_process)
        return PTR_ERR(E_INVAL);

    spinlock_acquire(&process->lock);

    /*
     * Another thread could well have killed the process.
     */
    if (process->state == SCHED_KILLED) {
        spinlock_release(&process->lock);
        return PTR_ERR(E_NOENT);
    }

    thread = kcalloc(1, sizeof(*thread), KMALLOC_KERNEL);
    if (thread == NULL) {
        spinlock_release(&process->lock);
        log_err("Failed to allocate new thread");
        return PTR_ERR(E_NOMEM);
    }

    kstack = vm_alloc(&kernel_address_space, KERNEL_STACK_SIZE,
                      VM_KERNEL_RW | VM_CLEAR);
    if (kstack == NULL) {
        log_err("Failed to allocate new kernel stack");
        err = E_NOMEM;
        goto thread_free;
    }

    thread_set_kernel_stack(thread, kstack);
    thread_set_mmu(thread, process->as->mmu);

    /* The initial thread's TID is equal to its containing process's PID */
    if (llist_is_empty(&process->threads))
        thread->tid = process->pid;
    else
        thread->tid = process_next_pid();

    err = arch_thread_init(thread, entrypoint, data, esp);
    if (err) {
        log_err("Failed to initialize new thread: %pe", &err);
        goto kstack_free;
    }

    thread->flags = flags;
    thread->process = process_get(process);

    llist_add(&process->threads, &thread->proc_this);

    spinlock_release(&process->lock);

    return thread;

kstack_free:
    vm_free(&kernel_address_space, kstack);
thread_free:
    kfree(thread);
    spinlock_release(&process->lock);
    return PTR_ERR(err);
}

static void thread_free(thread_t *thread)
{
    struct process *process = thread->process;

    log_info("terminating thread %d (%s)", thread->tid, process->name);

    /*
     * A thread cannot be free'd if it is currently running since it would
     * need to release the kernel stack it depends on.
     *
     * The thread's user memory should already have been free'd when marking
     * it as killed. If this patterns occurs, this means something went wrong
     * somewhere in our scheduler's logic.
     */
    if (unlikely(thread == current))
        PANIC("thread %d tried to kill itself", thread->tid);

    no_preemption_scope () {
        vm_free(&kernel_address_space, thread_get_kernel_stack(thread));

        /* initial thread is statically allocated so we can't kfree() it. */
        if (thread != &kernel_process_initial_thread)
            kfree(thread);

        /*
         * Release reference this threads holds onto the process.
         * This will also free the process if this is the process' last
         * running thread.
         */
        process_put(process);
    }
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

static void thread_kill_locked(thread_t *thread)
{
    struct process *process = thread->process;

    WARN_ON(!process->lock.locked);

    arch_thread_clear(thread);

    /*
     * Kernel user stack is shared across kthreads
     */
    if (!thread_is_kernel(thread)) {
        AS_ASSERT_OWNED(process->as);
        vm_free(process->as, thread_get_user_stack(thread));
    }

    /*
     * We just killed the process's last thread, we must clean the address
     * space while it is still loaded.
     */
    llist_remove(&thread->proc_this);
    if (llist_is_empty(&process->threads)) {
        arch_process_clear(process);
        address_space_clear(process->as);
    }

    /*
     * To make the implementation easier the actual 'killing' of the thread
     * is delayed until it is rescheduled (cf. thread_switch()).
     */
    thread->state = SCHED_KILLED;
}

void thread_kill(thread_t *thread)
{
    locked_scope (&thread->process->lock) {
        thread_kill_locked(thread);
    }

    if (thread == current)
        schedule_preempt();
}

static void __process_execute_in_userland(void *data)
{
    struct exec_params params;
    char * const argv[] = { data, NULL };
    char * const envp[] = { NULL };
    error_t err;

    params.exec_path = data;
    params.argv = argv;
    params.argc = 1;
    params.envp = envp;
    params.envpc = 0;

    err = execfmt_execute(&params);
    if (err) {
        log_err("%s: failed to execute: %pe", current->process->name, &err);
        return;
    }

    assert_not_reached();
}

struct thread *process_execute_in_userland(const char *exec_path)
{
    struct thread *thread;

    if (!vfs_exist(exec_path))
        return PTR_ERR(E_NOENT);

    thread = thread_fork(current, __process_execute_in_userland,
                         (void *)exec_path);
    if (IS_ERR(thread))
        return thread;

    sched_new_thread(thread);

    return thread;
}

struct thread *
thread_fork(struct thread *thread, thread_entry_t entrypoint, void *arg)
{
    struct process *new_process;
    struct thread *new;
    int flags;
    error_t err;

    /*
     * It is currently impossible to duplicate any other address space than the
     * one used by the current thread (loaded).
     */
    if (thread != current) {
        WARN("Cannot fork a non-running thread.");
        return PTR_ERR(E_INVAL);
    }

    /*
     * Forked processes can only be usermode processes.
     * The only kernel-mode process in the system is @ref kernel_process.
     */
    flags = thread->flags & ~THREAD_KERNEL;
    if (IS_KERNEL_ADDRESS(entrypoint) &&
        entrypoint != __process_execute_in_userland)
        return PTR_ERR(E_PERM);

    new_process = process_new();
    if (IS_ERR(new_process))
        return (void *)new_process;

    /* Duplicate the current thread's process's state */
    process_set_name(new_process, current->process->name, PROCESS_NAME_MAX_LEN);

    /*
     * Duplicate the current process' open files.
     */
    locked_scope (&current->process->files_lock) {
        for (size_t i = 0; i < PROCESS_FD_COUNT; ++i) {
            if (current->process->files[i])
                new_process->files[i] = file_get(current->process->files[i]);
        }
    }

    /* Duplicate the current thread's address space */
    address_space_copy_current(new_process->as);

    new = thread_spawn(new_process, entrypoint, arg,
                       thread_get_stack_pointer(thread), flags);
    if (new == NULL) {
        err = E_NOMEM;
        goto process_destroy;
    }

    locked_scope(&current->process->lock)
        llist_add(&current->process->children, &new_process->this);

    /*
     * Duplicate the current thread's stack.
     *
     * The content of the stack has already been copied when duplicating the
     * address space, so we just need to update the pointers.
     */
    thread_set_user_stack(new, thread_get_user_stack(thread));

    return new;

process_destroy:
    address_space_destroy(new_process->as);
    kfree(new_process);
    return PTR_ERR(err);
}

pid_t sys_fork(void)
{
    struct thread *fork;

    fork = thread_fork(current, (void *)current->frame.state.eip, NULL);
    if (IS_ERR(fork))
        return -ERR_FROM_PTR(fork);

    sched_new_thread(fork);

    return fork->tid;
}

NO_RETURN void thread_jump_to_userland(thread_entry_t entrypoint, void *data)
{
    arch_thread_jump_to_userland(entrypoint, data);
}

void thread_set_mmu(struct thread *thread, paddr_t mmu)
{
    arch_thread_set_mmu(thread, mmu);
}

void sys_exit(int status)
{
    process_kill(current->process, status);
}

pid_t sys_getpid(void)
{
    return current->process->pid;
}

/*
 * TODO: waitpid(): hande signals
 */
pid_t sys_waitpid(pid_t pid, int *stat_loc, int options)
{
    struct process *process = current->process;
    struct process *child;
    pid_t child_pid;
    uint8_t signo = 0;
    bool exists;
    bool found;

    /*
     * TODO:
     *
     * pid == 0: any child whose group id s equal to that of the calling process
     * pid < -1: any child whose group id s equal to the abs() of pid
     */
    if (pid < -1 || pid == 0) {
        not_implemented("wait GID pid");
        return -E_INVAL;
    }

    if (pid == INT_MIN)
        return -E_SRCH;

    while (true) {
        spinlock_acquire(&process->lock);
        found = false;
        FOREACH_LLIST_ENTRY(child, &process->children, this) {
            if (child->pid == pid)
                exists = true;
            if (READ_ONCE(child->state) != SCHED_ZOMBIE)
                continue;
            if (pid > 0 && child->pid != pid)
                continue;
            found = true;
            break;
        }
        spinlock_release(&process->lock);

        if (found)
            break;

        if (pid > 0 && !exists)
            return -E_CHILD;

        if (options & WNOHANG)
            return -E_SRCH;

        schedule();
    }

    if (stat_loc)
        *stat_loc = (child->exit_status << 8) | signo;

    child_pid = child->pid;
    process_collect_zombie(child);

    return child_pid;
}
