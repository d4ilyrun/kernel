#ifndef KERNEL_TYPES_H
#define KERNEL_TYPES_H

#define _SYS_TYPES_H // To avoid incompatibility with glibc during tests

#include <stdbool.h>
#include <stdint.h>

#define boolean(_x) (!!(_x))

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

#if ARCH == i686

/// Architecture independent type for physical addresses
typedef uintptr_t paddr_t;
/// Architecture independent type for virtual addresses
typedef uintptr_t vaddr_t;

#else
#error Unsuported architecture
#endif

typedef u32 pid_t;

#endif /* KERNEL_TYPES_H */
