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
clock_t timer_gettick(void);

/**
 *  @brief Wait a certain amount of miliseconds
 *  @warning Calls to this function are blocking
 */
void timer_wait_ms(time_t);

/** Convert a number of ticks to a time in miliseconds */
time_t timer_to_ms(time_t ticks);

/** Convert a number of ticks to a time in microseconds */
time_t timer_to_us(time_t ticks);

/** Get the number of time in ms elapsed since the machine started */
time_t gettime(void);

#endif /* KERNEL_DEVICES_TIMER_H */
