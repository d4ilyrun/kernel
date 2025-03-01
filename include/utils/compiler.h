/**
 * @file utlis/compiler.h
 *
 * @defgroup utils_compiler Compiler
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

#define assert_not_reached()                                             \
    do {                                                                 \
        log_warn("unreachable code reached: %s:%d", __FILE__, __LINE__); \
        __builtin_unreachable();                                         \
    } while (0)

#define ASM __asm__ volatile

#define ALWAYS_INLINE __attribute__((always_inline)) inline
#define PACKED __attribute__((__packed__))
#define SECTION(_section) __attribute__((section(_section)))
#define NO_DISCARD __attribute__((warn_unused_result))
#define MAYBE_UNUSED __attribute__((unused))
#define ALIAS(_function) __attribute__((alias(_function)))
#define ALIGNED(_alignment) __attribute__((aligned(_alignment)))
#define FORMAT(_type, _fmt, _args) __attribute__((format(_type, _fmt, _args)))
#define NO_RETURN __attribute__((noreturn))

/** Raises a compile time eror if \c _x is 0
 *  @return \c _x so that it can be used as a compile time known value
 */
#define NON_ZERO(_x) (1 + sizeof(struct { char size[(_x)-1]; }))

#endif /* UTILS_COMPILER_H */
