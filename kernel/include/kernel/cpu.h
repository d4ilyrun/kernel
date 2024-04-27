#ifndef KERNEL_CPU_H
#define KERNEL_CPU_H

#if ARCH == i686
#include <kernel/i686/cpu.h>
#else
#error Unknown CPU architecture.
#endif

#endif /* KERNEL_CPU_H */
