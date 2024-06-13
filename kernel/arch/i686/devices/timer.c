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
#include <utils/container_of.h>
#include <utils/macro.h>

/**
 * This is where we keep track of the number of intervals reported by the timer.
 *
 * This MUST be incremented EACH time we recieve an interrupt of type IRQ_TIMER.
 */
static volatile u64 timer_ticks_counter = 0;

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
 * @warning The frequency must be between 19 and 1.9MhZ
 *          Any other value will be adjusted back into this range.
 *
 * @param frequency The timer's frequency (Hz)
 */
void timer_start(u32 frequency)
{
    timer_kernel_frequency =
        pit_config_channel(PIT_CHANNEL_TIMER, frequency, PIT_RATE_GENERATOR);

    // Setup the timer's IRQ handler
    // It is responsible for updating our internal timer representation
    interrupts_set_handler(PIC_MASTER_VECTOR + IRQ_TIMER,
                           INTERRUPT_HANDLER(irq_timer), NULL);
    pic_enable_irq(IRQ_TIMER);
}

static DEFINE_INTERRUPT_HANDLER(irq_timer)
{
    UNUSED(data);

    if (timer_ticks_counter == UINT64_MAX) {
        log_warn("TIMER", "The internal timer has reached its max capacity.");
        log_warn("TIMER", "THIS WILL CAUSE AN OVERFLOW!");
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

    const node_t *next_wakeup = llist_head(sleeping_tasks);
    while (next_wakeup &&
           container_of(next_wakeup, process_t, this)->sleep.wakeup <=
               timer_ticks_counter) {
        next_wakeup = llist_pop(&sleeping_tasks);
        sched_unblock_process(container_of(next_wakeup, process_t, this));
        next_wakeup = llist_head(sleeping_tasks);
    }

    if (current_process->running.preempt <= timer_ticks_counter)
        schedule();

    return E_SUCCESS;
}

u64 timer_gettick(void)
{
    return timer_ticks_counter;
}

static int process_cmp_wakeup(const void *current_node, const void *cmp_node)
{
    const process_t *current = container_of(current_node, process_t, this);
    const process_t *cmp = container_of(cmp_node, process_t, this);

    RETURN_CMP(current->sleep.wakeup, cmp->sleep.wakeup);
}

void timer_wait_ms(u64 ms)
{
    const u64 start = timer_ticks_counter;
    const u64 end = start + (1000 * timer_kernel_frequency) / ms;

    current_process->sleep.wakeup = end;
    llist_insert_sorted(&sleeping_tasks, &current_process->this,
                        process_cmp_wakeup);
    sched_block_current_process();
}

u64 timer_to_ms(u64 ticks)
{
    return (1000 * ticks) / timer_kernel_frequency;
}
