#pragma once

#if defined(__i386__)
#define ARCH_IS_32_BITS
#define ARCH_WORD_SIZE 4
#define ARCH_LITTLE_ENDIAN
#elif defined(__x86_64__)
#define ARCH_IS_64_BITS
#define ARCH_WORD_SIZE 8
#define ARCH_LITTLE_ENDIAN
#else
#error Unsupported CPU architecture
#endif

#ifndef ARCH_LITTLE_ENDIAN
#error Unsupported endianness
#endif
