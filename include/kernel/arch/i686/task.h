#pragma once

/**
 * @file kernel/arch/i686/task.h
 *
 * @defgroup x86_task x86 - Task
 * @ingroup scheduling
 * @ingroup x86
 *
 * @{
 */

#include <kernel/types.h>

/**
 * Contains all the system-level information about a task
 *
 * These are the necessary information to schedule a
 */
typedef struct x86_task {
    u32 cr3;  ///< Physical address of the task's page directory
    u32 esp0; ///< Address of the top of the task's kernel stack
} task_t;
