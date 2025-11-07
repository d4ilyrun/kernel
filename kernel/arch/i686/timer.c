/**
 * @file kernel/arch/i686/timer.c
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

#include <kernel/timer.h>
#include <kernel/error.h>
#include <kernel/interrupts.h>
#include <kernel/logger.h>
#include <kernel/sched.h>

#include <kernel/arch/i686/devices/pic.h>
#include <kernel/arch/i686/devices/pit.h>

volatile clock_t timer_ticks_counter = 0;
volatile u32 timer_kernel_frequency = 0;

static INTERRUPT_HANDLER_FUNCTION(irq_timer);

error_t arch_timer_start(u32 frequency)
{
    error_t err;

    err = pit_config_channel(PIT_CHANNEL_TIMER, frequency, PIT_RATE_GENERATOR);
    if (err) {
        log_err("failed to config PIT timer channel");
        return err;
    }

    interrupts_set_handler(PIC_MASTER_VECTOR + IRQ_TIMER,
                           INTERRUPT_HANDLER(irq_timer), NULL);
    pic_enable_irq(IRQ_TIMER);

    return E_SUCCESS;
}

static INTERRUPT_HANDLER_FUNCTION(irq_timer)
{
    UNUSED(data);

    if (timer_tick())
        log_warn("INTERNAL TICKS COUNTER OVERFLOW");

    pic_eoi(IRQ_TIMER);

    if (!scheduler_initialized)
        return E_SUCCESS;

    // TODO: Use a separate and more modern timer for scheduler (LAPIC, HPET)
    //
    // We could be setting the next timer interrupt dynamically to match the
    // next due task, but:
    // * this is our main timekeeping source, and it would make it less accurate
    // * the PIT is too slow to re-program dynamically like this

    // TODO: Don't mingle IRQ and scheduling

    /*
     * Unblock waiting threads whose deadline has been reached, and preempt
     * the current thread if we reached the end of its timeslice.
     */
    sched_unblock_waiting_before(timer_ticks_counter);
    if (current->running.preempt <= timer_ticks_counter)
        schedule();

    return E_SUCCESS;
}
