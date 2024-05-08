/**
 * @file utlis/compiler.h
 *
 * @defgroup utils_compiler Compiler Arguments
 * @ingroup utils
 *
 * This file contains macros designed to be used with the compiler.
 * These includes: compile-time checks, type attributes, ...
 *
 * @{
 */

#ifndef UTILS_COMPILER_H
#define UTILS_COMPILER_H

#include "stringify.h"

/** @brief Compile-time assertion */
#define static_assert(cond, ...) \
    _Static_assert(cond, stringify(cond) __VA_OPT__(": ") __VA_ARGS__)

#define ASM __asm__ volatile

#define ALWAYS_INLINE __attribute__((always_inline)) inline
#define PACKED __attribute__((__packed__))
#define SECTION(_section) __attribute__((section(_section)))
#define NO_DISCARD __attribute__((warn_unused_result))
#define MAYBE_UNUSED __attribute__((unused))

#endif /* UTILS_COMPILER_H */
