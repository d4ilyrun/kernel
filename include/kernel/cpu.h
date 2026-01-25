#ifndef KERNEL_CPU_H
#define KERNEL_CPU_H

#include <kernel/types.h>

struct cpuinfo {
    /* Cache line flushing information */
    bool cache_flush_available;
    u32 cache_flush_line_size;
};

extern const struct cpuinfo *cpuinfo;

#if ARCH == i686
#include <kernel/arch/i686/cpu.h>
#else
#error Unknown CPU architecture.
#endif

/**
 * Expands to the return address of the current function, i.e. the
 * instruction pointer to which control will return after this function
 * finishes.
 */
#define __RET_IP (vaddr_t) __builtin_return_address(0)

/**
 * Expands to the instruction pointer of the location where the macro is used.
 */
#define __THIS_IP            \
    ({                       \
        __label__ __here;    \
    __here:                  \
        (vaddr_t) && __here; \
    })

#endif /* KERNEL_CPU_H */
