#ifndef DAILYRUN_ARCH_I686_CPU_H
#define DAILYRUN_ARCH_I686_CPU_H

#include <stdint.h>

/*
 * Structure containing all general purpose registers.
 *
 * This can be filled by the pusha instruction.
 */
struct x86_regs {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
};

/*
 * Frame pushed by the CPU before an interrupt.
 */
struct x86_interrupt_frame {
    uint32_t eip;
    uint32_t cs;
    uint32_t flags;
    uint32_t esp;
    uint32_t ss;
};

#endif /* DAILYRUN_ARCH_I686_CPU_H */
