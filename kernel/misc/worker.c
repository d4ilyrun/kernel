#define LOG_DOMAIN "worker"

#include <kernel/logger.h>
#include <kernel/sched.h>
#include <kernel/worker.h>

#include <utils/macro.h>

static void worker_entrypoint(void *cookie)
{
    struct worker *worker = cookie;

    INFINITE_LOOP () {
        worker->function(worker->data);
        worker->done = true;
        waitqueue_dequeue_all(&worker->queue);
        sched_block_thread(worker->thread);
    }
}

error_t worker_init(struct worker *worker)
{
    struct thread *thread;

    if (worker->thread)
        return E_BUSY;

    INIT_WORKER(*worker);

    thread = thread_spawn(&kernel_process, worker_entrypoint, worker,
                          THREAD_KERNEL);
    if (thread == NULL) {
        log_err("failed to spawn worker thread");
        return E_NOMEM;
    }

    worker->thread = thread;
    sched_block_thread(thread);

    return E_SUCCESS;
}

void worker_release(struct worker *worker)
{
    WARN_ON(!waitqueue_is_empty(&worker->queue));

    if (worker->thread == current) {
        WARN("A worker is trying to release itself");
        return;
    }

    /** TODO: Find a cleaner way to kill a thread that wasn't alive before */
    no_scheduler_scope() {
        sched_new_thread(worker->thread);
        worker->thread->state = SCHED_KILLED;
    }
}

void worker_start(struct worker *worker, thread_entry_t function, void *data)
{
    WARN_ON(!waitqueue_is_empty(&worker->queue));

    if (worker_running(worker)) {
        log_warn("worker has already been started");
        return;
    }

    worker->done = false;
    worker->data = data;
    worker->function = function;

    sched_unblock_thread(worker->thread);
}

void worker_wait(struct worker *worker)
{
    if (worker->done)
        return;

    waitqueue_enqueue(&worker->queue, current);
}
