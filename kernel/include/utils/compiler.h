#ifndef UTILS_COMPILER_H
#define UTILS_COMPILER_H

#define ASM __asm__ volatile

#define static_assert _Static_assert // NOLINT

#define ALWAYS_INLINE __attribute__((always_inline)) inline
#define PACKED __attribute__((__packed__))
#define SECTION(_section) __attribute__((section(_section)))
#define NO_DISCARD __attribute__((warn_unused_result))
#define MAYBE_UNUSED __attribute__((unused))

#endif /* UTILS_COMPILER_H */
