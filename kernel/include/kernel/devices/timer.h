/** \header timer.h
 *
 * \brief Interface with the i8254 PIT.
 *
 * Programmable Interval Counter.
 *
 * Any interaction done with the timer should be done through
 * the functions defined inside this header.
 *
 * The i8254 PIT has an internal frequency of 1.19 MHz, and 3 separate
 * counters. Each counter must be configured with one of 7 modes, and a
 * frequency. To be more precise we can only specify the divider, which applied
 * to the internal PIT frequency (1.9MHz) to obtain the actual final counter
 * frequency. Each time the counter reaches the computed limit, it triggers a
 * \link IRQ_TIMER.
 *
 * We keep track of the number of IRQ_TIMER recieved, each one representing a
 * kernel timer 'tick'.
 *
 * Warning:
 *
 *   For now, we do not allow for dynamically re-configuring the timer.
 *   We forcefully set it to mode 2 (1 interrupt per tick), and only allow
 *   the user to change its frequency.
 *
 * Reference manual can be found here:
 *   * https://k.lse.epita.fr/data/8254.pdf
 *   * https://k.lse.epita.fr/internals/8254_controller.html
 *   * https://wiki.osdev.org/Programmable_Interval_Timer
 */

#ifndef KERNEL_DEVICES_TIMER_H
#define KERNEL_DEVICES_TIMER_H

#include <kernel/interrupts.h>

#include <utils/types.h>

#define TIMER_INTERNAL_FREQUENCY (1193182)

#define TIMER_TICK_FREQUENCY (1000) // 1KHz

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
 *
 * TODO: Handle the timer IRQ
 */
void timer_start(u32 frequency);

/** Read the current value inside the timer's counter */
u16 timer_read(void);

/** Return the number of intervals that passed since the timer started */
u64 timer_gettick(void);

/** Wait a certain amount of miliseconds
 *  @warning Calls to this function are blocking
 */
void timer_wait_ms(u64);

/** Convert a number of ticks to a time in miliseconds */
u64 timer_to_ms(u64 ticks);

DEFINE_INTERRUPT_HANDLER(irq_timer);

#endif /* KERNEL_DEVICES_TIMER_H */
