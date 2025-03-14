#define LOG_DOMAIN "process"

#include <kernel/error.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/mmu.h>
#include <kernel/process.h>

#include <kernel/arch/i686/gdt.h>

#include <utils/compiler.h>
#include <utils/macro.h>

#include <string.h>

/* defined in process.S */
NO_RETURN void __arch_process_jump_to_userland(process_entry_t entrypoint,
                                               segment_selector cs,
                                               segment_selector ds, u32 esp);

NO_RETURN void
arch_process_jump_to_userland(process_entry_t entrypoint, void *data)
{
    segment_selector ds = {.index = GDT_ENTRY_USER_DATA, .rpl = 3};
    segment_selector cs = {.index = GDT_ENTRY_USER_CODE, .rpl = 3};
    u32 ustack_top;

    UNUSED(data);

    /* Reset user stack */
    memset((void *)current_process->context.esp_user, 0, KERNEL_STACK_SIZE);

    log_dbg("jump to userland");
    ustack_top = current_process->context.esp_user + KERNEL_STACK_SIZE;
    __arch_process_jump_to_userland(entrypoint, cs, ds, ustack_top);

    assert_not_reached();
}

/** Finish setting up the process before jumping to its entrypoint
 *
 * This function initializes components that could not be setup from another
 * process's address space.
 *
 * The return to the process's entry point is done implicitely through the
 * artificial stack setup in @arch_process_create.
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
void arch_process_entrypoint(process_entry_t entrypoint, void *data)
{
    u32 *ustack = NULL;

    if (!vmm_init(current_process->vmm, USER_MEMORY_START, USER_MEMORY_END))
        log_err("Failed to initilize VMM (%s)", current_process->name);

    ustack = kcalloc(KERNEL_STACK_SIZE, 1, KMALLOC_DEFAULT);
    if (ustack == NULL) {
        log_err("Failed to allocate new user stack");
        goto error_exit;
    }

    current_process->context.esp_user = (u32)ustack;

    if (process_is_kernel(current_process)) {
        entrypoint(data);
    } else {
        arch_process_jump_to_userland(entrypoint, data);
    }

error_exit:
    process_kill(current_process);
}

bool arch_process_create(process_t *process, process_entry_t entrypoint,
                         void *data)
{
    u32 *kstack = NULL;
    paddr_t cr3;

    // Allocate a kernel stack for the new process
    kstack = kcalloc(KERNEL_STACK_SIZE, 1, KMALLOC_KERNEL);
    if (kstack == NULL) {
        log_err("Failed to allocate new kernel stack");
        return false;
    }

    cr3 = mmu_new_page_directory();
    if (IS_ERR(cr3)) {
        log_err("Failed to create new page directory");
        goto release_kernel_stack;
    }

    process->context.cr3 = cr3;
    process->context.esp0 = (u32)kstack;

    // Setup basic stack frame to be able to start the process using 'ret'
    // 1. Return into 'arch_process_entrypoint'
    // 2. From entrypoint, jump to the process's entrypoint

#define KSTACK(_i) kstack[KERNEL_STACK_SIZE / sizeof(u32) - (_i)]

    // Stack frame for arch_process_entrypoint
    KSTACK(0) = (u32)data;       // arg2
    KSTACK(1) = (u32)entrypoint; // arg1
    KSTACK(2) = 0;               // nuke ebp

    // Stack frame for arch_process_switch
    KSTACK(3) = (u32)arch_process_entrypoint;
    KSTACK(4) = 0;               // edi
    KSTACK(5) = 0;               // esi
    KSTACK(6) = 0;               // ebx
    KSTACK(7) = (u32)&KSTACK(3); // ebp

    // Set new process's stack pointer to the top of our manually created
    // context_switching stack
    process->context.esp = (u32)&KSTACK(7);

#undef KSTACK

    return true;

release_kernel_stack:
    kfree(kstack);
    return false;
}

void arch_process_free(process_t *process)
{
    kfree((void *)process->context.esp0);
    // TODO: release MMU
}
