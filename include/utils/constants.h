#pragma once

/**
 * @file utils/constants.h
 *
 * @defgroup utils_constants Constants
 * @ingroup utils
 *
 * Common known constants
 *
 * @{
 */

/** @brief Used for conversions from seconds to another time unit @{ */
#define SEC(_x) _x
#define MS(_s) (SEC(_s) / 1000)
#define US(_s) (MS((_s)) / 1000)
#define NS(_s) (US((_s)) / 1000)
/** @} */

/** @brief Used for conversions to seconds from another time unit @{ */
#define FROM_SEC(_x) _x
#define FROM_MS(_s) (1000 * SEC(_s))
#define FROM_US(_s) (1000 * MS((_s)))
#define FROM_NS(_s) (1000 * US((_s)))
/** @} */
