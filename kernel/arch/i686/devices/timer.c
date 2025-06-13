/**
 * @file kernel/arch/i686/devices/timer.c
 *
 * @addtogroup timer_x86
 *
 * @brief We use the @ref PIT channel 0 for our internal timer.
 *
 * ## Implementation
 *
 * We keep track of the number of IRQ_TIMER recieved, each one representing a
 * kernel timer 'tick'.
 *
 * @{
 */

#define LOG_DOMAIN "timer"

#include <kernel/cpu.h>
#include <kernel/devices/timer.h>
#include <kernel/error.h>
#include <kernel/interrupts.h>
#include <kernel/logger.h>
#include <kernel/sched.h>
#include <kernel/types.h>

#include <kernel/arch/i686/devices/pic.h>
#include <kernel/arch/i686/devices/pit.h>

#include <libalgo/linked_list.h>
#include <utils/constants.h>
#include <utils/container_of.h>
#include <utils/macro.h>

/**
 * This is where we keep track of the number of intervals reported by the timer.
 *
 * This MUST be incremented EACH time we recieve an interrupt of type IRQ_TIMER.
 */
static volatile clock_t timer_ticks_counter = 0;

/** The current frequency of the PIT channel associated with the timer. */
static volatile u32 timer_kernel_frequency = 0;

static DEFINE_INTERRUPT_HANDLER(irq_timer);

static DECLARE_LLIST(sleeping_tasks);

/**
 * Start the timer.
 *
 * At each interval, the timer will trigger an IRQ_TIMER
 * interrupt, which MUST be handled to update our internal
 * timer tick value.
 *
 * @warning The frequency must be between 19 and 1.9MhZ.
 *
 * @param frequency The timer's frequency (Hz)
 */
void timer_start(u32 frequency)
{
    error_t err;

    err = pit_config_channel(PIT_CHANNEL_TIMER, frequency, PIT_RATE_GENERATOR);
    if (err)
        PANIC("Failed to start kernel timer.");

    // Setup the timer's IRQ handler
    // It is responsible for updating our internal timer representation
    interrupts_set_handler(PIC_MASTER_VECTOR + IRQ_TIMER,
                           INTERRUPT_HANDLER(irq_timer), NULL);
    pic_enable_irq(IRQ_TIMER);
}

static DEFINE_INTERRUPT_HANDLER(irq_timer)
{
    struct thread *next_wakeup;

    UNUSED(data);

    if (timer_ticks_counter == INT64_MAX) {
        log_warn("The internal timer has reached its max capacity.");
        log_warn("THIS WILL CAUSE AN OVERFLOW!");
    }

    timer_ticks_counter += 1;

    pic_eoi(IRQ_TIMER);

    // TODO: Use a separate and more modern timer for scheduler (LAPIC, HPET)
    //
    // We could be setting the next timer interrupt dynamically to match the
    // next due task, but:
    // * this is our main timekeeping source, and it would make it less accurate
    // * the PIT is too slow to re-program dynamically like this

    // TODO: Don't mingle IRQ and scheduling

    if (!scheduler_initialized)
        return E_INVAL;

    while (!llist_is_empty(&sleeping_tasks)) {
        next_wakeup = container_of(llist_first(&sleeping_tasks), struct thread,
                                   this);
        if (next_wakeup->sleep.wakeup > timer_ticks_counter)
            break;

        llist_pop(&sleeping_tasks);
        sched_unblock_thread(next_wakeup);
    }

    if (current->running.preempt <= timer_ticks_counter)
        schedule();

    return E_SUCCESS;
}

time_t timer_gettick(void)
{
    return timer_ticks_counter;
}

static int process_cmp_wakeup(const void *current_node, const void *cmp_node)
{
    const thread_t *current = container_of(current_node, thread_t, this);
    const thread_t *cmp = container_of(cmp_node, thread_t, this);

    RETURN_CMP(current->sleep.wakeup, cmp->sleep.wakeup);
}

void timer_wait_ms(time_t ms)
{
    const clock_t start = timer_ticks_counter;
    const clock_t end = start + (1000 * timer_kernel_frequency) / ms;

    current->sleep.wakeup = end;
    llist_insert_sorted(&sleeping_tasks, &current->this, process_cmp_wakeup);
    sched_block_thread(current);
}

time_t timer_to_ms(time_t ticks)
{
    return (1000 * ticks) / timer_kernel_frequency;
}

time_t timer_to_us(time_t ticks)
{
    return (US(ticks)) / timer_kernel_frequency;
}

time_t gettime(void)
{
    // FIXME: replace with timer_get_ms() or sth along those lines
    return timer_to_ms(timer_gettick());
}
