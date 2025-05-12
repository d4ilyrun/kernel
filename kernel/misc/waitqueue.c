#include <kernel/sched.h>
#include <kernel/waitqueue.h>

#include <utils/container_of.h>

bool waitqueue_is_empty(struct waitqueue *queue)
{
    bool empty = true;

    locked_scope (&queue->lock)
        empty = queue_is_empty(&queue->queue);

    return empty;
}

void waitqueue_enqueue_locked(struct waitqueue *queue, struct thread *thread)
{
    /*
     * We MUST prevent rescheduling while performing this switch.
     * In the case where we are queueing the current thread, if the schedule()
     * event happens once the thread has been marked as waiting but before
     * it has been enqueued, the thread will never be rescheduled again and
     * it will be forever lost.
     */
    no_scheduler_scope () {
        thread->state = SCHED_WAITING;
        queue_enqueue(&queue->queue, &thread->this);
        /* Release the lock held by the caller BEFORE rescheduling */
        spinlock_release(&queue->lock);
    }

    if (thread == current)
        schedule_preempt();
}

const struct thread *waitqueue_peek(struct waitqueue *queue)
{
    const struct thread *thread = NULL;
    const node_t *node;

    locked_scope (&queue->lock) {
        if (!queue_is_empty(&queue->queue)) {
            node = queue_peek(&queue->queue);
            thread = container_of(node, struct thread, this);
        }
    }

    return thread;
}

struct thread *waitqueue_dequeue(struct waitqueue *queue)
{
    struct thread *thread = NULL;
    node_t *node;

    locked_scope (&queue->lock) {
        if (!queue_is_empty(&queue->queue)) {
            node = queue_dequeue(&queue->queue);
            thread = container_of(node, struct thread, this);
        }
    }

    if (thread)
        sched_new_thread(thread);

    return thread;
}

size_t waitqueue_dequeue_all(struct waitqueue *queue)
{
    const bool old_if = scheduler_lock();
    struct thread *thread = NULL;
    size_t count = 0;
    node_t *node;

    locked_scope (&queue->lock) {
        while (!queue_is_empty(&queue->queue)) {
            node = queue_dequeue(&queue->queue);
            thread = container_of(node, struct thread, this);
            sched_new_thread(thread);
            count += 1;
        }
    }

    scheduler_unlock(old_if);

    return count;
}
