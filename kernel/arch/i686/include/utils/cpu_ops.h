#ifndef KERNEL_I686_UTILS_CPU_OPS_H
#define KERNEL_I686_UTILS_CPU_OPS_H

#include <utils/compiler.h>
#include <utils/map.h>
#include <utils/types.h>

// Read from a 32-bits register
#define READ_REGISTER_OPS(_reg)                  \
    static ALWAYS_INLINE u32 read_##_reg()       \
    {                                            \
        u32 res;                                 \
        ASM("movl %%" #_reg ", %0" : "=r"(res)); \
        return res;                              \
    }

// Write into a 32-bits register
#define WRITE_REGISTER_OPS(_reg)                      \
    static ALWAYS_INLINE void write_##_reg(u32 value) \
    {                                                 \
        ASM("movl %0, %%" #_reg : : "r"(value));      \
    }

#define CPU_32BIT_REGISTERS cr0, cr1, cr2, cr3, cr4

MAP(READ_REGISTER_OPS, CPU_32BIT_REGISTERS, )
MAP(WRITE_REGISTER_OPS, CPU_32BIT_REGISTERS, )

#undef CPU_32BIT_REGISTERS
#undef WRITE_REGISTER_OPS
#undef READ_REGISTER_OPS

#endif /* KERNEL_I686_UTILS_CPU_OPS_H */
