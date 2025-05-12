/**
 * @brief Semaphore
 * @defgroup semaphore Semaphore
 * @ingroup kernel
 */

#ifndef KERNEL_SEMAPHORE_H
#define KERNEL_SEMAPHORE_H

#include <kernel/spinlock.h>
#include <kernel/waitqueue.h>

/** A semaphore */
struct semaphore {
    spinlock_t lock;
    struct waitqueue waitqueue;
    unsigned int count;
};

typedef struct semaphore semaphore_t;

/** Semaphore init value.
 *  @param count The number of users that can take this semaphore simultaneously
 */
#define SEMAPHORE_INIT(_count)     \
    ((struct semaphore){           \
        .count = (_count),         \
        .lock = {.locked = false}, \
        .waitqueue = {.lock = {.locked = false}, .queue = {NULL, NULL}}})

/** Initialize a semaphore */
#define INIT_SEMAPHORE(_name, _count) _name = SEMAPHORE_INIT(_count)

/** Declare and initialize a semaphore */
#define DECLARE_SEMAPHORE(_name, _count) \
    struct semaphore _name = SEMAPHORE_INIT(_count)

/** Mutex init value. */
#define MUTEX_INIT SEMAPHORE_INIT(1)

/** Initialize a mutex. */
#define INIT_MUTEX(_name) INIT_SEMAPHORE(_name, 1)

/** Declare and initialize a mutex. */
#define DECLARE_MUTEX(_name) semaphore_t _name = MUTEX_INIT

/** Decrement the semaphore's counter.
 *
 *  If the semaphore's counter has already reached 0, the process
 *  will be put to sleep until it becomes available again.
 */
struct semaphore *semaphore_acquire(struct semaphore *);

/** Increase the semaphore's counter.
 *  If a process had requested this semaphore, it will be waken up.
 */
void semaphore_release(struct semaphore *);

#endif /* KERNEL_SEMAPHORE_H */
