#include <kernel/semaphore.h>

struct semaphore *semaphore_acquire(struct semaphore *semaphore)
{
    spinlock_acquire(&semaphore->lock);

    if (semaphore->count > 0) {
        semaphore->count -= 1;
        spinlock_release(&semaphore->lock);
    } else {
        /*
         * We need to acquire the waitqeue's lock before unlocking the
         * sempahore, because if not done so the semaphore could be released
         * before we add the current process into the waiting queue. If this
         * were to occur, the process may not be signaled ever again
         * if the counter never reaches 0.
         */
        spinlock_acquire(&semaphore->waitqueue.lock);
        spinlock_release(&semaphore->lock);
        waitqueue_enqueue_locked(&semaphore->waitqueue, current);
    }

    return semaphore;
}

void semaphore_release(struct semaphore *semaphore)
{
    unsigned int count;

    locked_scope (&semaphore->lock) {
        count = semaphore->count++;
        if (count == 0)
            waitqueue_dequeue(&semaphore->waitqueue);

        spinlock_release(&semaphore->lock);
    }
}
