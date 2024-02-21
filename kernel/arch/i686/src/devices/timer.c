/** \file timer.c
 *
 * We use the PIT's channel 0 for our internal timer.
 *
 * We keep track of the number of IRQ_TIMER recieved, each one representing a
 * kernel timer 'tick'.
 *
 * \see pit.h
 */

#include <kernel/devices/timer.h>
#include <kernel/interrupts.h>
#include <kernel/logger.h>

#include <kernel/i686/cpu_ops.h>
#include <kernel/i686/devices/pic.h>
#include <kernel/i686/devices/pit.h>

#include <utils/macro.h>
#include <utils/types.h>

/**
 * This is where we keep track of the number of intervals reported by the timer.
 *
 * This MUST be incremented EACH time we recieve an interrupt of type IRQ_TIMER.
 */
static volatile u64 timer_ticks_counter = 0;

/** The current frequency of the PIT channel associated with the timer. */
static volatile u32 timer_kernel_frequency = 0;

static DEFINE_INTERRUPT_HANDLER(irq_timer);

/**
 * Start the timer.
 *
 * At each interval, the timer will trigger an IRQ_TIMER
 * interrupt, which MUST be handled to update our internal
 * timer tick value.
 *
 * @warn The frequency must be between 19 and 1.9MhZ
 *       Any other value will be adjusted back into this range.
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
                           INTERRUPT_HANDLER(irq_timer));
    pic_enable_irq(IRQ_TIMER);
}

static DEFINE_INTERRUPT_HANDLER(irq_timer)
{
    UNUSED(frame);

    if (timer_ticks_counter == UINT64_MAX) {
        log_warn("TIMER", "The internal timer has reached its max capacity.");
        log_warn("TIMER", "THIS WILL CAUSE AN OVERFLOW!");
    }

    timer_ticks_counter += 1;

    pic_eoi(IRQ_TIMER);
}

u64 timer_gettick(void)
{
    return timer_ticks_counter;
}

void timer_wait_ms(u64 ms)
{
    const u64 start = timer_ticks_counter;
    const u64 end = start + (1000 * timer_kernel_frequency) / ms;

    WAIT_FOR(timer_ticks_counter >= end);
}

u64 timer_to_ms(u64 ticks)
{
    return (1000 * ticks) / timer_kernel_frequency;
}
