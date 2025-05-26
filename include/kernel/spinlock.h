#pragma once

/**
 * @addtogroup kernel
 *
 * @{
 */

#include <utils/compiler.h>
#include <utils/macro.h>

#include <stdbool.h>

/**
 * @struct spinlock
 * @brief Spinlock
 */
typedef struct spinlock {
    bool locked;
} spinlock_t;

/** Default init value (unlocked) */
#define __SPINLOCK_INIT \
    {                   \
        .locked = false \
    }
#define SPINLOCK_INIT ((spinlock_t)__SPINLOCK_INIT)

/** Initialize a spinlock */
#define __INIT_SPINLOCK(_lock) _lock = __SPINLOCK_INIT
#define INIT_SPINLOCK(_lock) _lock = SPINLOCK_INIT

/** Declare a spinlock and initialize it */
#define DECLARE_SPINLOCK(_lock) spinlock_t _lock = SPINLOCK_INIT

/** @brief Try to acquire a spinlock, or wait until it is free */
static ALWAYS_INLINE spinlock_t *spinlock_acquire(spinlock_t *lock)
{
    WAIT_FOR(!__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE));
    return lock;
}

/** @brief Release a spinlock for others to take it */
static ALWAYS_INLINE void spinlock_release(spinlock_t *lock)
{
    __atomic_clear(&lock->locked, __ATOMIC_RELEASE);
}

typedef struct {
    spinlock_t *lock;
    bool done;
} scope_lock_t;

static inline scope_lock_t scope_lock_constructor(spinlock_t *lock)
{
    return (scope_lock_t){
        .lock = spinlock_acquire(lock),
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
