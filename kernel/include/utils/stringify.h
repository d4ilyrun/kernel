#ifndef UTILS_STRINGIFY_H
#define UTILS_STRINGIFY_H

/** @brief Preprocess an expression into a raw string
 */

#define stringify1(_x) #_x
#define stringify(_x) stringify1(_x)

#endif /* UTILS_STRINGIFY_H */
