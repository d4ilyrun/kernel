#define LOG_DOMAIN "process"

#include <kernel/error.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/mmu.h>
#include <kernel/process.h>

#include <kernel/arch/i686/gdt.h>

#include <utils/compiler.h>

void arch_process_jump_to_userland(process_entry_t entrypoint,
                                   segment_selector cs, segment_selector ds,
                                   u32 esp);

/** Finish setting up the process before jumping to its entrypoint
 *
 * This function initializes components that could not be setup from another
 * process's address space.
 *
 * The return to the process's entry point is done implicitely through the
 * artificial stack setup in @arch_process_create.
 */
void arch_process_entrypoint(process_entry_t entrypoint, void *data)
{
    if (!vmm_init(current_process->vmm, USER_MEMORY_START, USER_MEMORY_END))
        log_err("Failed to initilize VMM (%s)", current_process->name);

    if (process_is_kernel(current_process)) {
        entrypoint(data);
    } else {
        void *user_stack = kcalloc(KERNEL_STACK_SIZE, sizeof(u8),
                                   KMALLOC_DEFAULT);
        if (user_stack == NULL) {
            log_err("failed to allocate user stack for '%s'",
                    current_process->name);
        } else {
            segment_selector ds = {.index = GDT_ENTRY_USER_DATA, .rpl = 3};
            segment_selector cs = {.index = GDT_ENTRY_USER_CODE, .rpl = 3};
            log_info("jump to userland");
            arch_process_jump_to_userland(entrypoint, cs, ds, (u32)user_stack);
            __builtin_unreachable();
        }
    }

    process_kill(current_process);
}

bool arch_process_create(process_t *process, process_entry_t entrypoint,
                         void *data)
{
    // Allocate a kernel stack for the new process
    u32 *kstack = kmalloc(KERNEL_STACK_SIZE, KMALLOC_KERNEL);
    if (kstack == NULL) {
        log_err("Failed to allocate new kernel stack");
        return false;
    }

    paddr_t cr3 = mmu_new_page_directory();
    if (IS_ERR(cr3)) {
        log_err("Failed to create new page directory");
        kfree(kstack);
        return false;
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

    return process;
}

void arch_process_free(process_t *process)
{
    kfree((void *)process->context.esp0);
    // TODO: release MMU
}
