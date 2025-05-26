/**
 * @brief Waiting queue
 *
 * @defgroup waitqueue Waitqueue
 * @ingroup scheduling
 *
 * A waitqueue is used to list threads that are waiting for a ressource before
 * being rescheduled.
 *
 * Some examples are:
 *
 * * A mutex must wakeup all thread that tried to take the lock when
 *   the owner eventually releases it
 * * An IP packet may need to wait for an ARP request to complete for
 *   it to retreive the destination's MAC address
 *
 *
 * All accesses to a waitqueue are placed behind a lock to make this API
 * thread safe.
 *
 * ## Usage
 *
 * A waitqueue is used exactly as you would a regular queue. The only difference
 * is that when a thread is queued it is automatically marked as waiting, and
 * resumes running once dequeued.
 *
 * @{
 */

#ifndef KERNEL_WAITQUEUE_H
#define KERNEL_WAITQUEUE_H

#include <kernel/process.h>
#include <kernel/spinlock.h>

#include <libalgo/queue.h>

/** Waiting Queue */
struct waitqueue {
    spinlock_t lock; /*<! Synchronization lock */
    queue_t queue;   /*<! The queue of waiting threads */
};

/** Default init value */
#define __WAITQUEUE_INIT(_queue)                                             \
    {                                                                        \
        __INIT_SPINLOCK(.lock), .queue = __QUEUE_INIT(&(_queue).queue.head), \
    }
#define WAITQUEUE_INIT(_queue) ((struct waitqueue)__WAITQUEUE_INIT(_queue))

/** Initialize a waitqueue */
#define __INIT_WAITQUEUE(_queue) _queue = __WAITQUEUE_INIT(_queue)
#define INIT_WAITQUEUE(_queue) _queue = WAITQUEUE_INIT(_queue)

/** Declare and initialize a waitqueue */
#define DECLARE_WAITQUEUE(_queue) struct waitqueue INIT_WAITQUEUE(_queue)

/** Check whether anyone is waiting for this event to finish */
bool waitqueue_is_empty(struct waitqueue *);

/** Mark a thread as waiting for the event to finish.
 *
 *  This function is equivalent to @ref waitqueue_enqueue. It should be
 *  called when holding the queue's lock.
 *
 *  NOTE: This function releases the lock held by the caller
 */
void waitqueue_enqueue_locked(struct waitqueue *, struct thread *);

/** Mark a thread as waiting for the event to finish */
#define waitqueue_enqueue(waitqueue, thread)         \
    ({                                               \
        spinlock_acquire(&(waitqueue)->lock);        \
        waitqueue_enqueue_locked(waitqueue, thread); \
    })

/** @return The first thread inside the waitqueue */
const struct thread *waitqueue_peek(struct waitqueue *);

/** Wakeup the first thread inside the queue.
 *  @return The thread that was waken up
 */
struct thread *waitqueue_dequeue(struct waitqueue *);

/** Wakeup all the threads inside the queue
 *  @return The number of threads that were waken up
 */
size_t waitqueue_dequeue_all(struct waitqueue *);

#endif /* KERNEL_WAITQUEUE_H */

/** @} */
