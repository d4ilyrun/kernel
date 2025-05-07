#define LOG_DOMAIN "worker"

#include <kernel/logger.h>
#include <kernel/sched.h>
#include <kernel/worker.h>

static void worker_entrypoint(void *cookie)
{
    struct worker *worker = cookie;

    worker->function(worker->data);

    worker->done = true;
    waitqueue_dequeue_all(&worker->queue);

    thread_kill(current);
}

void worker_start(struct worker *worker, thread_entry_t function, void *data)
{
    struct thread *thread;

    WARN_ON(!waitqueue_is_empty(&worker->queue));

    if (!worker->done) {
        log_warn("worker has already been started");
        return;
    }

    worker->done = false;
    worker->data = data;
    worker->function = function;

    thread = thread_spawn(&kernel_process, worker_entrypoint, worker,
                          THREAD_KERNEL);

    sched_new_thread(thread);
}

void worker_wait(struct worker *worker)
{
    if (worker->done)
        return;

    waitqueue_enqueue(&worker->queue, current);
}
