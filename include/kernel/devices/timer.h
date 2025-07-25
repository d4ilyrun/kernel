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
#include <kernel/time.h>

/** The frequency used for the timer (in Hz) */
#define HZ CLOCK_PER_SECOND

/** @brief Used for conversions from seconds to another time unit @{ */
#define SEC(_x) (_x)
#define MS(_s) (1000 * SEC(_s))
#define US(_s) (1000 * MS((_s)))
#define NS(_s) (1000 * US((_s)))
/** @} */

/** @brief Used for conversions to seconds from another time unit @{ */
#define SEC_TO_SEC(_s) SEC(_s)
#define MS_TO_SEC(_s) (SEC(_s) / 1000)
#define US_TO_SEC(_s) (MS((_s)) / 1000)
#define NS_TO_SEC(_s) (US((_s)) / 1000)
/** @} */

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
 * @brief Start the timer
 * @param frequency The timer's frequency (Hz)
 */
void timer_start(u32 frequency);

/** @return the number of timer intervals elapsed since startup */
clock_t timer_gettick(void);

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

#endif /* KERNEL_DEVICES_TIMER_H */
