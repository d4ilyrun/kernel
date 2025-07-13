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

/**
 * Finish setting up the thread before jumping to its entrypoint
 *
 * This function initializes components that could not be setup from another
 * process's address space.
 *
 * The return to the thread's entry point is done implicitely through the
 * artificial stack setup in @arch_thread_create.
 */
static void
arch_thread_entrypoint(thread_entry_t entrypoint, void *data, void *esp)
{
    u32 *ustack = NULL;

    /* scheduler was locked by the previous thread before starting this one */
    scheduler_preempt_enable(true);

    /*
     * Allocate the user stack.
     *
     * This is necessary when there is no pre-allocated user stack (creating
     * a new thread of an existing process).
     *
     * TODO: This should in theory not be needed since:
     * 1. Kernel threads all share a common pre-allocated user stack
     * 2. User-threads can only be created using clone(), which needs the user
     *    to pass in a pre-allocated user-stack as argument.
     */
    if (!thread_get_user_stack(current)) {
        if (thread_is_kernel(current)) {
            /* Kernel threads share a common user stack. */
            ustack = thread_get_user_stack(&kernel_process_initial_thread);
        } else {
            ustack = vm_alloc(current->process->as, USER_STACK_SIZE,
                              VM_READ | VM_WRITE | VM_CLEAR);
            if (ustack == NULL) {
                log_err("Failed to allocate new user stack");
                goto error_exit;
            }
        }

        thread_set_user_stack(current, ustack);
    }

    /*
     * When kicking-off a forked thread the original thread's stack pointer
     * should have been specified during the thread's creation.
     */
    if (esp)
        thread_set_stack_pointer(current, esp);

    if (IS_KERNEL_ADDRESS(entrypoint)) {
        entrypoint(data);
    } else {
        if (thread_is_kernel(current)) {
            log_err("%d: kernel thread cannot execute userland function",
                    current->tid);
            goto error_exit;
        }
        arch_thread_jump_to_userland(entrypoint, data);
    }

    /*
     * Userland processes should be killed using _exit() and never return
     * until here.
     */
    if (!thread_is_kernel(current))
        assert_not_reached();

error_exit:
    thread_kill(current);
}

error_t arch_thread_init(thread_t *thread, thread_entry_t entrypoint,
                         void *data, void *esp)
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

    return E_SUCCESS;
}

void arch_thread_clear(thread_t *thread)
{
    UNUSED(thread);
}

void arch_process_clear(struct process *process)
{
    UNUSED(process);
}

void arch_thread_set_mmu(struct thread *thread, paddr_t mmu)
{
    thread->context.cr3 = mmu;
}
