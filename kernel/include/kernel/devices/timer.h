/** \header timer.h
 *
 * Any interaction done with the timer should be done through
 * the functions defined inside this header.
 *
 * We use the PIT's channel 0 for our internal timer.
 *
 * We keep track of the number of IRQ_TIMER recieved, each one representing a
 * kernel timer 'tick'.
 *
 * \see pit.h
 */

#ifndef KERNEL_DEVICES_TIMER_H
#define KERNEL_DEVICES_TIMER_H

#include <utils/types.h>

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
 */
void timer_start(u32 frequency);

/** Return the number of intervals that passed since the timer started */
u64 timer_gettick(void);

/** Wait a certain amount of miliseconds
 *  @warning Calls to this function are blocking
 */
void timer_wait_ms(u64);

/** Convert a number of ticks to a time in miliseconds */
u64 timer_to_ms(u64 ticks);

#endif /* KERNEL_DEVICES_TIMER_H */
