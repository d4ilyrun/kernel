#ifndef UTILS_COMPILER_H
#define UTILS_COMPILER_H

#define ASM __asm__ volatile

#define PACKED __attribute__((__packed__))

#define static_assert _Static_assert // NOLINT

#define ALWAYS_INLINE inline __attribute__((always_inline))

#endif /* UTILS_COMPILER_H */
