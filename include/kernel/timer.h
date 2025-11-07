/**
 * @file kernel/timer.h
 *
 * @defgroup time Time keeping
 * @ingroup kernel_time
 *
 * # Time
 *
 * ## Timer
 *
 * Any interaction done with the timer should be done through
 * the functions defined inside this header.
 *
 * @{
 */

#ifndef KERNEL_DEVICES_TIMER_H
#define KERNEL_DEVICES_TIMER_H

#include <kernel/time.h>
#include <kernel/types.h>

/** The frequency used for the timer (in Hz) */
#define HZ CLOCK_PER_SECOND

/** @brief Compute the number of ticks in a given time frame @{ */
#define SEC_TO_TICKS(_time) SEC((_time) * HZ)
#define MS_TO_TICKS(_time) MS_TO_SEC(SEC_TO_TICKS(_time))
#define US_TO_TICKS(_time) US_TO_SEC(SEC_TO_TICKS(_time))
#define NS_TO_TICKS(_time) NS_TO_SEC(SEC_TO_TICKS(_time))
/** @} */

/** @brief Convert a number of ticks into a regular time unit @{ */
#define TICKS_TO_SEC(_ticks) SEC((_ticks) / HZ)
#define TICKS_TO_MS(_ticks) MS(TICKS_TO_SEC(_ticks))
#define TICKS_TO_US(_ticks) US(TICKS_TO_SEC(_ticks))
#define TICKS_TO_NS(_ticks) NS(TICKS_TO_SEC(_ticks))
/** @} */

/**
 * This is where we keep track of the number of intervals reported by the timer.
 *
 * This MUST be incremented EACH time we recieve an interrupt from the global
 * timer.
 */
extern volatile clock_t timer_ticks_counter;

/**
 * Frequency of the global timekeeping timer.
 */
extern volatile u32 timer_kernel_frequency;

/**
 * @brief Start the global timekeeping timer
 * @param frequency The timer's frequency (Hz)
 */
void timer_start(u32 frequency);

/** @return the number of timer intervals elapsed since startup */
static inline clock_t timer_gettick(void)
{
    return timer_ticks_counter;
}

/**
 * Increment the timekeeping timer's tick count.
 * @return \c true if an overflow occured.
 */
static inline bool timer_tick(void)
{
    clock_t old_ticks = timer_ticks_counter;
    timer_ticks_counter += 1;
    return old_ticks > timer_ticks_counter;
}

/** @return the number of miliseconds elapsed since startup. */
static inline time_t timer_get_ms(void)
{
    return TICKS_TO_MS(timer_gettick());
}

/** @return the number of microseconds elapsed since startup. */
static inline time_t timer_get_us(void)
{
    return TICKS_TO_US(timer_gettick());
}

/** @return the number of nanoseconds elapsed since startup. */
static inline time_t timer_get_ns(void)
{
    return TICKS_TO_NS(timer_gettick());
}

/** Fill a timespec structure with the current time of the day.
 *
 * FIXME: Change this to use the system's clock once clock_settime().
 *        For now we'll use the system's timer instead.
 */
static inline void clock_get_time(struct timespec *time)
{
    time_t ns = timer_get_ns();

    time->tv_sec = NS_TO_SEC(ns);
    time->tv_nsec = ns % NS(1);
}

/**
 *  @brief Wait a certain amount of miliseconds
 *  @warning Calls to this function are blocking
 */
void timer_wait_ms(time_t);

/*
 * Wait for a certain amount of time.
 * Can be used in a non-preemptible context.
 *
 * TODO: Make it safe to call in an un-interruptible
 *       context by computing the timing of a for loop
 *       and using this instead.
 */
void timer_delay_ms(time_t);

#endif /* KERNEL_DEVICES_TIMER_H */
