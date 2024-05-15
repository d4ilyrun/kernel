#include <kernel/cpu.h>
#include <kernel/sched.h>

#include <libalgo/queue.h>
#include <utils/container_of.h>

/** This is a placeholder until we implement an actual spinlock mechanism */
typedef struct spinlock {
    uint8_t irq; // stub: spinlock for IRQ with SMP
} spinlock_t;

typedef struct scheduler {

    /** The runqueue
     * All processes inside this queue are ready to run and could theoretically
     * be switched to at any moment.
     */
    queue_t ready;
    spinlock_t lock;
} scheduler_t;

static scheduler_t scheduler;

/** IDLE task running when there are no other task ready */
static process_t *idle_process;

void schedule(void)
{
    node_t *next_node = queue_dequeue(&scheduler.ready);

    if (next_node == NULL)
        return;

    process_t *next = container_of(next_node, process_t, this);

    // If some tasks are ready, do not reschedule the idle task
    if (next == idle_process && queue_peek(&scheduler.ready)) {
        next_node = queue_dequeue(&scheduler.ready);
        next = container_of(next_node, process_t, this);
        queue_enqueue(&scheduler.ready, &idle_process->this);
    }

    queue_enqueue(&scheduler.ready, &current_process->this);

    process_switch(next);
}

static void idle_task(void *data __attribute__((unused)))
{
    while (1) {
        hlt();
    }
}

void scheduler_init(void)
{
    idle_process = process_create("<< IDLE >>", idle_task);
    // use the largest PID possible to avoid any conflict later on
    idle_process->pid = (pid_t)-1;
    sched_new_process(idle_process);
}

void sched_new_process(process_t *process)
{
    process->state = SCHED_RUNNING;
    queue_enqueue(&scheduler.ready, &process->this);
}
