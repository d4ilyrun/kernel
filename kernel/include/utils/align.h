#ifndef UTILS_ALIGN_H
#define UTILS_ALIGN_H

#include "types.h"

/// @brief Align a value onto another one.
///
/// If the original value is not aligned, its value will be
/// rounded **up* to the next aligned value.
static inline u32 align(u32 value, u32 alignment)
{
    if (alignment == 0)
        return value;

    u32 offset = value % alignment;
    if (offset)
        value += alignment - offset; // WARNING: Offset can occur here!

    return value;
}

#endif /* UTILS_ALIGN_H */
