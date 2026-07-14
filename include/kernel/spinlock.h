#pragma once

/**
 * @addtogroup kernel
 *
 * @{
 */

#include <kernel/cpu.h>
#include <kernel/types.h>

#include <utils/compiler.h>
#include <utils/macro.h>

#include <stdbool.h>

typedef struct spinlock {
    bool locked;
    vaddr_t owner; /* Instruction pointer to where this lock was acquired. */
} spinlock_t;

#define __SPINLOCK_INIT \
    {                   \
        .locked = false \
    }

#define SPINLOCK_INIT ((spinlock_t)__SPINLOCK_INIT)

#define __INIT_SPINLOCK(_lock) _lock = __SPINLOCK_INIT
#define INIT_SPINLOCK(_lock) _lock = SPINLOCK_INIT

#define DECLARE_SPINLOCK(_lock) spinlock_t _lock = SPINLOCK_INIT

spinlock_t * __spinlock_acquire(spinlock_t *lock, vaddr_t owner);
#define spinlock_acquire(lock) __spinlock_acquire(lock, __THIS_IP)

void spinlock_release(spinlock_t *lock);

/** Check whether a lock is currently held by someone. */
static inline bool spinlock_is_held(const spinlock_t *lock)
{
    return __atomic_load_n(&lock->locked, __ATOMIC_ACQUIRE);
}

typedef struct {
    spinlock_t *lock;
    bool done;
} scope_lock_t;

static inline scope_lock_t scope_lock_constructor(spinlock_t *lock)
{
    return (scope_lock_t){
        .lock = __spinlock_acquire(lock, __RET_IP),
        .done = false,
    };
}

static inline void scope_lock_destructor(scope_lock_t *guard)
{
    spinlock_release(guard->lock);
}

/** Define a scope that is guarded by a spinlock.
 *
 *  This is particularily useful when taking and releasing a lock
 *  in a sequential manner. For example:
 *
 *  locked_scope(&list_lock) {
 *      llist_add(&global_list, &new_item);
 *  }
 *
 *  In this example the @c list_lock spinlock will automatically be taken when
 *  entering the scope, and also automatically released when leaving.
 *
 *  WARNING: As this macro uses a for loop to function, any 'break' directive
 *  placed inside it will break out of the guarded scope instead of that of its
 *  containing loop.
 */
#define locked_scope(_lock)                                  \
    for (scope_lock_t guard CLEANUP(scope_lock_destructor) = \
             scope_lock_constructor(_lock);                  \
         !guard.done; guard.done = true)
