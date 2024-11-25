#ifndef KERNEL_TYPES_H
#define KERNEL_TYPES_H

#define _SYS_TYPES_H // To avoid incompatibility with glibc during tests

#include <arch.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define boolean(_x) (!!(_x))

/*
 * These macros have no real effect.
 * They are just here to make it visually clear
 * when a variable uses a specific endianness.
 */
#define __be /** Specify that a variable uses big endianness */
#define __le /** Specify that a variable uses little endianness */

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

typedef float f32;  //< 32b floating point value
typedef double f64; //< 64b floating point value

typedef long int ssize_t;

/** Guaranteed to be the size of a native word, regardless of the architecture
 */
#ifdef ARCH_IS_32_BITS
typedef u32 native_t;
#elif defined(ARCH_IS_64_BITS)
typedef u64 native_t;
#else
#error Unsuported architecture
#endif

/// Architecture independent type for physical addresses
typedef native_t paddr_t;
/// Architecture independent type for virtual addresses
typedef native_t vaddr_t;

typedef u32 pid_t;
typedef u64 timestamp_t;

/** An IPv4 address */
typedef uint32_t ipv4_t;

/** Comparison function over two generic objects
 *  @return 0 if both are equal, -1 if left is inferior, +1 if it is superior
 */
typedef int (*compare_t)(const void *left, const void *right);
#define COMPARE_EQ 0
#define COMPARE_LESS -1
#define COMPARE_GREATER 1

#endif /* KERNEL_TYPES_H */
