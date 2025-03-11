#ifndef KERNEL_ARCH_I686_PROCESS_H
#define KERNEL_ARCH_I686_PROCESS_H

/**
 * @file kernel/arch/i686/process.h
 *
 * @defgroup x86_process Processes - x86
 * @ingroup process
 * @ingroup x86
 *
 * @{
 */

#include <kernel/types.h>

/**
 * Contains all the system-level information about a task
 * @struct x86_thread
 */
typedef struct x86_thread {

    u32 cr3; ///< Physical address of the process's page directory

    /**
     * @brief Address of the top of the thread's kernel stack.
     *
     * This is the value used by the kernel to locate the
     * stack to use when switching from ring3 -> ring0.
     * It should be loaded inside the current cpu's
     * @link tss TSS.ESP0 @endlink when switching thread.
     */
    u32 esp0;

    /**
     * @brief Address of the top of the user stack
     * This is only valid for user threads.
     */
    u32 esp_user;

    u32 esp; ///< The current stack pointer of the thread

} thread_context_t;

#endif /* KERNEL_ARCH_I686_PROCESS_H */
