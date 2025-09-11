#pragma once

/**
 * @addtogroup kernel
 *
 * @{
 */

#include <kernel/cpu.h>
#include <kernel/types.h>

#ifdef CONFIG_SPINLOCK_DEBUG
#include <kernel/devices/timer.h>
#include <kernel/logger.h>
#endif

#include <utils/compiler.h>
#include <utils/macro.h>

#include <stdbool.h>

/* Default timeout: 30s */
#define SPINLOCK_DEBUG_STALL_TIMEOUT MS(30)

/**
 * @struct spinlock
 * @brief Spinlock
 */
typedef struct spinlock {
    bool locked;
#ifdef SPINLOCK_DEBUG
    /** Instruction pointer where this lock was last acquired. */
    vaddr_t owner;
#endif
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

#ifdef CONFIG_SPINLOCK_DEBUG

/** @brief Try to acquire a spinlock, or wait until it is free */
static ALWAYS_INLINE spinlock_t *
__spinlock_acquire(spinlock_t *lock, vaddr_t owner)
{
    time_t start = timer_get_ms();

    while (__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE)) {
        if (timer_get_ms() - start > SPINLOCK_DEBUG_STALL_TIMEOUT) {
            WARN("stall detected on spinlock (owner: %ps)",
                 (void *)lock->owner);
            start = timer_get_ms();
        }
    }

    lock->owner = owner;

    return lock;
}

#else

/** @brief Try to acquire a spinlock, or wait until it is free */
static ALWAYS_INLINE spinlock_t *
__spinlock_acquire(spinlock_t *lock, vaddr_t owner)
{
    UNUSED(owner);

    WAIT_FOR(!__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE));

    return lock;
}

#endif

#define spinlock_acquire(lock) __spinlock_acquire(lock, __THIS_IP)

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
