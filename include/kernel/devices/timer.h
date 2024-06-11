/**
 * @file kerneldevices/timer.h
 *
 * @defgroup timer Timer
 * @ingroup kernel
 *
 * # Timer
 *
 * Any interaction done with the timer should be done through
 * the functions defined inside this header.
 *
 * The underlying implementation is architecture dependent and, as such,
 * can be found inside the corresponding `arch` subfolder.
 *
 * @{
 */

#ifndef KERNEL_DEVICES_TIMER_H
#define KERNEL_DEVICES_TIMER_H

#include <kernel/types.h>

/** The frequency used for the timer (in Hz) */
#define TIMER_TICK_FREQUENCY (1000) // 1KHz

/**
 * @brief Start the timer
 * @param frequency The timer's frequency (Hz)
 */
void timer_start(u32 frequency);

/** Return the number of intervals that passed since the timer started */
u64 timer_gettick(void);

/**
 *  @brief Wait a certain amount of miliseconds
 *  @warning Calls to this function are blocking
 */
void timer_wait_ms(u64);

/** Convert a number of ticks to a time in miliseconds */
u64 timer_to_ms(u64 ticks);

/** Convert a number of ticks to a time in microseconds */
u64 timer_to_us(u64 ticks);

#endif /* KERNEL_DEVICES_TIMER_H */
