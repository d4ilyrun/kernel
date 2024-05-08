#ifndef UTILS_STRINGIFY_H
#define UTILS_STRINGIFY_H

/**
 * @brief Preprocess an expression into a raw string
 * @ingroup utils
 */
#define stringify(_x) stringify1(_x)
#define stringify1(_x) #_x

#endif /* UTILS_STRINGIFY_H */
