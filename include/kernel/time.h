/**
 * @file kernel/time.h
 * @ingroup kernel_time
 *
 * @{
 */

#ifndef KERNEL_TIME_H
#define KERNEL_TIME_H

/* The number of clock ticks in a second (1KHz). */
#define CLOCK_PER_SECOND 1000

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

#endif /* KERNEL_TIME_H */

/** @} */
