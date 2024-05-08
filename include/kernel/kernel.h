#ifndef KERNEL_H
#define KERNEL_H

/**
 * @file kernel/kernel.h
 *
 * @defgroup kernel Kernel functions and structures
 *
 * # Kernel
 *
 * This is a kernel (not micro nor monolithic yet), written from scratch on my
 * free time to learn more about systems and OS designs.
 *
 * ## Disclaimer
 *
 * This kernel serves only one purpose: learning.
 *
 * The implementations and design choices are likely to be ... questionable,
 * but this is part of the process. There exists better sources of inspiration
 * and better examples than this project.
 *
 * ## Structure
 *
 * I try to keep arcihtecture specific code and what can be generic separated.
 * You can find the architecture depedent code (specific structures, devices,
 * mechanisms ...) inside the kernel/arch folder.
 *
 */

/**
 * @defgroup x86 Arch - x86
 * @ingroup kernel
 *
 * # x86
 *
 * Architecture specific code for x86
 */

#endif /* KERNEL_H */
