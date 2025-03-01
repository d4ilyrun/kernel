#pragma once

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
 * @struct x86_process
 */
typedef struct x86_process {

    u32 cr3; ///< Physical address of the process's page directory

    /**
     * @brief Address of the top of the process's kernel stack.
     *
     * This is the value used by the kernel to locate the
     * stack to use when switching from ring3 -> ring0.
     * It should be loaded inside the current cpu's
     * @link tss TSS.ESP0 @endlink when switching process.
     */
    u32 esp0;

    /**
     * @brief Address of the top of the user stack
     * This is only valid for user processes.
     */
    u32 esp_user;

    u32 esp; ///< The current stack pointer of the process

} process_context_t;
