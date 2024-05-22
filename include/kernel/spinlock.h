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

/** @brief Try to acquire a spinlock, or wait until it is free */
static ALWAYS_INLINE void spinlock_acquire(spinlock_t *lock)
{
    WAIT_FOR(__atomic_test_and_set(&lock->locked, __ATOMIC_ACQUIRE));
}

/** @brief Release a spinlock for others to take it */
static ALWAYS_INLINE void spinlock_release(spinlock_t *lock)
{
    __atomic_clear(&lock->locked, __ATOMIC_RELEASE);
}
