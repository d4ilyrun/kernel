#pragma once

/**
 * @brief Process management
 * @file kernel/process.h
 *
 * This file defines the data structures and functions for managing processes
 * and threads in a multithreading model.
 *
 * A process is an instance of a program that is being executed by one or more
 * threads. Each process has its own unique address space, which means that it
 * has its own virtual memory space that is separate from other processes. This
 * allows each process to have its own private memory space, which is not
 * accessible to other processes. A process also has its own set of system
 * resources, such as open files descriptors.
 *
 * A thread is an execution context that is created within a process.
 * Threads within the same process share the same address space. They do not
 * share the same execution context (registers, stack).
 *
 * Both processes and threads are identified using a unique id: PID and TID.
 * Threads do not exist without a containing process, and processes are forced
 * to always contain at least one alive thread.
 *
 * The file provides functions for creating and killing processes, creating and
 * killing threads, as well as switching between them.
 *
 * @defgroup process Processes
 * @ingroup scheduling
 *
 * @{
 */

#include <kernel/file.h>
#include <kernel/types.h>
#include <kernel/vmm.h>

#include <libalgo/linked_list.h>
#include <utils/compiler.h>

#include <string.h>

#if ARCH == i686
#include <kernel/arch/i686/process.h>
#else
#error Unsupported arcihtecture
#endif

/** The max length of a process's name */
#define PROCESS_NAME_MAX_LEN 32U

/** Maximum number of files that one process can have open at any one time. */
#define PROCESS_FD_COUNT 32

struct address_space;

/** A function used as an entry point when creating a new thread
 *
 * @param data Generic data passed on to the function when starting
 * the thread
 * @note We never return from this function
 */
typedef void (*thread_entry_t)(void *data);

/** The different states a thread can be in
 *  @enum thread_state
 */
typedef enum thread_state {
    SCHED_RUNNING, ///< Currently running (or ready to run)
    SCHED_WAITING, ///< Currently waiting for a resource (timer, lock ...)
    SCHED_ZOMBIE,  ///< Thread waiting to be collected by its parent process.
    SCHED_KILLED,  ///< The thread has been killed waiting to be destroyed
} thread_state_t;

/**
 * @brief A single process
 *
 * The struct contains information about the process, including its name,
 * unique ID and resources context (address space, file descriptors, ...).
 * It also contains a list of all the process's active threads.
 *
 * @struct process
 */
struct process {
    char name[PROCESS_NAME_MAX_LEN]; /*!< The thread's name */
    pid_t pid;                       /*!< Process' unique ID */

    struct address_space *as; /*!< The process's address space */

    llist_t threads;        /*!< Linked list of the process' active threads */
    llist_t children;       /*!< Linked list of the process' active children */
    node_t this;            /*< Node inside the parent's list of children */

    size_t refcount; /*!< Reference count to this process.
                         We only kill a process once all of its threads have
                         been released. */

    /** Open file descriptors table.
     *
     * This table is lock protected by @ref files_lock, one must **always**
     * take this lock when accessing a process' open files (read AND write).
     */
    struct file *files[PROCESS_FD_COUNT];
    spinlock_t files_lock; /*!< Lock for @ref open_files */

    thread_state_t state;
    uint8_t exit_status; /** Transmitted to the parent process during wait() */

    spinlock_t lock;
};

/**
 * @brief A single thread.
 *
 * The struct contains information about the thread's execution state.
 * This state is architecture dependant, so its actual definition is present
 * inside the arch specific process header file.
 *
 * Generic information about a thread include its current state (used by the
 * scheduler), information about its containing process and feature flags.
 *
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
    thread_state_t state; /*!< Thread's current state, used by the scheduler */

    struct process *process; /*!< Containing process */
    node_t proc_this; /*!< Linked list used by the process to list threads */

    pid_t tid; /*!< Thread ID */
    u32 flags; /*!< Combination of \ref thread_flags values */

    node_t this; /*!< Intrusive linked list used by the scheduler */

    /** Information relative to the current state of the thread */
    union {
        /** For running threads only */
        struct {
            /** End of the currently running thread's timeslice */
            clock_t preempt;
        } running;
        /** For sleeping threads only */
        struct {
            clock_t wakeup; /*!< Time when it should wakeup (in ticks) */
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

/** The initial thread is the thread created along with the process.
 *  The TID of the initial thread is the same as its process's PID.
 */
static ALWAYS_INLINE bool thread_is_initial(thread_t *thread)
{
    return thread->tid == thread->process->pid;
}

/** Set the thread's current stack pointer */
static inline void thread_set_stack_pointer(struct thread *thread, void *stack)
{
    arch_thread_set_stack_pointer(&thread->context, stack);
}

/** Get the thread's current stack pointer */
static inline void *thread_get_stack_pointer(struct thread *thread)
{
    return arch_thread_get_stack_pointer(&thread->context);
}

/** Set the thread's current base pointer */
static inline void thread_set_base_pointer(struct thread *thread, void *ptr)
{
    arch_thread_set_base_pointer(&thread->context, ptr);
}

/** Get the thread's current base pointer */
static inline void *thread_get_base_pointer(struct thread *thread)
{
    return arch_thread_get_base_pointer(&thread->context);
}

/** Set a thread's curent interrupt frame. */
static inline void
thread_set_interrupt_frame(thread_t *thread,
                           const struct interrupt_frame *frame)
{
    arch_thread_set_interrupt_frame(&thread->context, frame);
}

/** Set the thread's kernel stack bottom address */
static inline void thread_set_kernel_stack(struct thread *thread, void *stack)
{
    arch_thread_set_kernel_stack_top(&thread->context,
                                     stack + KERNEL_STACK_SIZE);
}

/** Get the thread's kernel stack top address */
static inline void *thread_get_kernel_stack_top(const struct thread *thread)
{
    return arch_thread_get_kernel_stack_top(&thread->context);
}

/** Get the thread's kernel stack bottom address */
static inline void *thread_get_kernel_stack(const struct thread *thread)
{
    void *top = thread_get_kernel_stack_top(thread);
    if (!top)
        return NULL;

    return top - KERNEL_STACK_SIZE;
}

/** Set the thread's user stack bottom address */
static inline void thread_set_user_stack(struct thread *thread, void *stack)
{
    arch_thread_set_user_stack_top(&thread->context, stack + USER_STACK_SIZE);
}

/** Get the thread's user stack top address */
static inline void *thread_get_user_stack_top(const struct thread *thread)
{
    return arch_thread_get_user_stack_top(&thread->context);
}

/** Get the thread's user stack bottom address */
static inline void *thread_get_user_stack(const struct thread *thread)
{
    void *top = thread_get_user_stack_top(thread);
    if (!top)
        return NULL;

    return top - USER_STACK_SIZE;
}

/**
 * Read the thread's return address when exiting the current interrupt context.
 */
static inline void *
thread_get_interrupt_return_address(const struct thread *thread)
{
    return arch_thread_get_interrupt_return_address(&thread->context);
}

/** Process used when starting up the kernel.
 *
 * It is necesary to define it statically, since memory management functions are
 * not yet set up when first starting up.
 *
 * We should not reference this process anymore once we have an up and running
 * scheduler.
 */
extern struct process kernel_process;
extern struct thread kernel_process_initial_thread;

/** The init process is the very first executed userland process.
 *
 *  It is the parent of all other userland processes, and all zombie processes
 *  are attached to it when their parent dies.
 *
 *  The init process uses the reserved PID 1.
 */
extern struct process *init_process;

/** Initialize the kernel process's address space.
 *
 *  The kernel process's instance being defined satically, it is required to
 *  be explicitely initialize during the startup phase so that it can be used
 *  just like any other process later on.
 */
void process_init_kernel_process(void);

/** Register an open file inside the process's open file descriptor table.
 *  @return The registered file's index inside the open file descriptor table.
 */
int process_register_file(struct process *, struct file *);

/** Remove an open file from the process's open file descriptor table. */
error_t process_unregister_file(struct process *, int fd);

/**
 * @return A reference to the file description at index @ref fd
 * inside @ref process 's file descriptor table.
 */
struct file *process_file_get(struct process *, int fd);

/** Release a file description retreived using @ref process_file_get(). */
static inline void process_file_put(struct process *process, struct file *file)
{
    UNUSED(process);
    file_put(file);
}

/** Run an executable.
 *
 *  As the kernel should never run external executables, this function instead
 *  creates a new userland process that will in turn be used to execute the
 *  executable.
 *
 *  @param exec_path Absolute path to the executable file
 */
struct thread *process_execute_in_userland(const char *exec_path);

/***/
static inline void process_set_name(struct process *process, const char *name,
                                    size_t size)
{
    strlcpy(process->name, name, MIN(size + 1, PROCESS_NAME_MAX_LEN));
}

/** The currently running thread */
extern thread_t *current;

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
 * @param process The process the newly created thread belongs to
 * @param entrypoint The function called when starting the thread
 * @param data Data passed to the entry function (can be NULL)
 * @param esp The value to place inside the stack pointer register before
 *            first kicking off the thread. This is mainly useful when forking
 *            an existing thread. Ignored if NULL.
 * @param ebp The value placed inside the stack base pointer register before
 *            first kicking off the thread. Ignored if NULL.
 * @param flags Feature flags: a combination of \ref thread_flags enum values
 */
thread_t *thread_spawn(struct process *, thread_entry_t, void *data,
                       void *esp, void *ebp, u32 flags);

/** Start executing code in userland
 *
 * This function resets the user execution context (eip, stack content).
 * It then changes the privilege level to be that of userland, and jumps
 * onto the given address.
 *
 * @info This serves as a temporary equivalent to the execve syscall for testing
 *       purposes.
 *
 * @param stack_pointer The stack pointer used when jumping to userland
 * @param base_pointer  The base pointer used when jumping to userland
 * @param entrypoint The entrypoint address to jump onto
 * @param data       The data passed as an argument to the 'entrypoint' function
 *                   (ignored)
 */
NO_RETURN void thread_jump_to_userland(void *stack_pointer, void *base_pointer,
                                       thread_entry_t, void *);

/** Set the MMU address saved inside the thread's structure.
 *  @note This function does not change the MMU currently in use,
 *        see @ref mmu_load for this instead.
 */
void thread_set_mmu(struct thread *thread, paddr_t mmu);

/** Effectively kill a thread
 *
 *  * Free all private memory used by the thread
 *  * Synchronize resources (write to files, ...)
 */
void thread_kill(thread_t *);

/** Create a new fork of the given thread.
 *
 * A new process is created to execute the forked thread.
 * The address space of the original thread's process is duplicated
 * inside the newly created process.
 *
 * If the duplicated process had multiple threads, only the one that
 * called this function is replicated.
 *
 * @return The newly created thread
 */
struct thread *thread_fork(struct thread *, thread_entry_t, void *);

/** @defgroup arch_process Processes - arch specifics
 *  @ingroup x86_process
 */

/** @} */
