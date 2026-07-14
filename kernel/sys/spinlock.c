#define LOG_DOMAIN "spinlock"

#include <kernel/spinlock.h>
#include <kernel/logger.h>
#include <kernel/timer.h>
#include <kernel/sched.h>

/* Default timeout: 30s */
#define SPINLOCK_DEBUG_STALL_TIMEOUT MS(30)

/* Check whether it is safe to acquire a lock inside the current context.
 *
 * It is unsafe to acquire a lock inside an uninterruptible context. This is
 * because it could cause a livelock situation if the initial test_and_set()
 * is to fail and the lock's owner was running on the same core as the current
 * non-preemptible thread.
 */
static inline bool spinlock_safe_context(void)
{
    if (unlikely(!interrupts_initialized))
        return true;

    if (unlikely(!scheduler_initialized))
        return interrupts_enabled();

    return interrupts_enabled() && sched_preemptible();
}

spinlock_t * __spinlock_acquire(spinlock_t *lock, vaddr_t owner)
{
    time_t start = timer_get_ms();

    PANIC_ON(!spinlock_safe_context(),
             "spinlock_acquire() called in an invalid context");

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

void spinlock_release(spinlock_t *lock)
{
    __atomic_clear(&lock->locked, __ATOMIC_RELEASE);
}
