#define LOG_DOMAIN "process"

#include <kernel/error.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/mmu.h>
#include <kernel/process.h>
#include <kernel/sched.h>

#include <kernel/arch/i686/gdt.h>

#include <utils/compiler.h>
#include <utils/container_of.h>
#include <utils/macro.h>

#include <string.h>

/* defined in process.S */
NO_RETURN void __arch_thread_jump_to_userland(thread_entry_t entrypoint,
                                              segment_selector cs,
                                              segment_selector ds, u32 esp);

NO_RETURN void
arch_thread_jump_to_userland(thread_entry_t entrypoint, void *data)
{
    segment_selector ds = {.index = GDT_ENTRY_USER_DATA, .rpl = 3};
    segment_selector cs = {.index = GDT_ENTRY_USER_CODE, .rpl = 3};

    UNUSED(data);

    /* Reset user stack */
    memset(thread_get_user_stack(current), 0, KERNEL_STACK_SIZE);
    __arch_thread_jump_to_userland(entrypoint, cs, ds,
                                   (vaddr_t)thread_get_user_stack_top(current));

    assert_not_reached();
}

/** Finish setting up the thread before jumping to its entrypoint
 *
 * This function initializes components that could not be setup from another
 * process's address space.
 *
 * The return to the thread's entry point is done implicitely through the
 * artificial stack setup in @arch_thread_create.
 *
 * FIXME: We should not take an entrypoint when creating a new process.
 *
 * The logic behind that is :
 * 1- There should be only one kernel process, and many kernel threads
 * 2- The only way to create a new process is to fork. Forking duplicates
 *    the running process's execution context, and continue the execution
 *    where it left => No need to specify an entrypoint, just exit out of
 *    the syscall
 * 3- When wanting to run a new executable, the forked process uses the
 *    execve syscall. This is where the real entrypoint is. Once again,
 *    no need to create a new process, simply free VMMs and FDs (man 2),
 *    setup a new stack and return to userland. But in no way do we create
 *    a new process to run the executable!
 *
 * This should be fixed when refactoring the task model (add threads & delayed
 * work).
 */
static void
arch_thread_entrypoint(thread_entry_t entrypoint, void *data, void *esp)
{
    u32 *ustack = NULL;

    /* scheduler was locked by the previous thread before starting this one */
    scheduler_preempt_enable(true);

    if (thread_is_initial(current)) {
        if (address_space_init(current->process->as))
            log_err("Failed to initilize VMM (%s)", current->process->name);
    }

    ustack = kcalloc(KERNEL_STACK_SIZE, 1, KMALLOC_DEFAULT);
    if (ustack == NULL) {
        log_err("Failed to allocate new user stack");
        goto error_exit;
    }

    thread_set_user_stack(current, ustack);

    /*
     * When kicking-off a forked thread the original thread's stack pointer
     * should have been specified during the thread's creation.
     */
    if (esp)
        thread_set_stack_pointer(current, esp);

    if (thread_is_kernel(current)) {
        entrypoint(data);
    } else {
        arch_thread_jump_to_userland(entrypoint, data);
    }

error_exit:
    thread_kill(current);
}

bool arch_thread_init(thread_t *thread, thread_entry_t entrypoint, void *data,
                      void *esp)
{
    u32 *kstack = thread_get_kernel_stack_top(thread);

    // Setup basic stack frame to be able to start the thread using 'ret'
    // 1. Return into 'arch_thread_entrypoint'
    // 2. From entrypoint, jump to the thread's entrypoint

#define KSTACK(_i) kstack[-(_i) - 1]

    // Stack frame for arch_thread_entrypoint
    KSTACK(0) = (u32)esp;        // arg3
    KSTACK(1) = (u32)data;       // arg2
    KSTACK(2) = (u32)entrypoint; // arg1
    KSTACK(3) = 0;               // nuke ebp

    // Stack frame for arch_thread_switch
    KSTACK(4) = (u32)arch_thread_entrypoint;
    KSTACK(5) = 0;               // edi
    KSTACK(6) = 0;               // esi
    KSTACK(7) = 0;               // ebx
    KSTACK(8) = (u32)&KSTACK(4); // ebp

    // Set new thread's stack pointer to the top of our manually created
    // context_switching stack
    thread_set_stack_pointer(thread, &KSTACK(8));

#undef KSTACK

    return true;
}

void arch_thread_free(thread_t *thread)
{
    kfree((void *)thread->context.esp_user);
}

void arch_process_free(struct process *process)
{
    UNUSED(process);
}

void arch_thread_set_mmu(struct thread *thread, paddr_t mmu)
{
    thread->context.cr3 = mmu;
}
