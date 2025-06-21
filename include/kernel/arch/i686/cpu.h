#ifndef KERNEL_ARCH_I686_UTILS_CPU_OPS_H
#define KERNEL_ARCH_I686_UTILS_CPU_OPS_H

#include <kernel/types.h>

#include <utils/compiler.h>
#include <utils/map.h>

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

#define CPU_32BIT_REGISTERS \
    cr0, cr1, cr2, cr3, cr4, esp, cs, ds, es, fs, gs, ss, eax

MAP(READ_REGISTER_OPS, CPU_32BIT_REGISTERS)
MAP(WRITE_REGISTER_OPS, CPU_32BIT_REGISTERS)

#undef CPU_32BIT_REGISTERS
#undef WRITE_REGISTER_OPS
#undef READ_REGISTER_OPS

/* Write a single byte at a given I/O port address. */
static ALWAYS_INLINE void outb(uint16_t port, uint8_t val)
{
    ASM("out %0,%1" : : "a"(val), "Nd"(port) : "memory");
}

/* Write 2 bytes at a given I/O port address. */
static ALWAYS_INLINE void outw(uint16_t port, uint16_t val)
{
    ASM("out %0,%1" : : "a"(val), "Nd"(port) : "memory");
}

/* Write 4 bytes at a given I/O port address. */
static ALWAYS_INLINE void outl(uint16_t port, uint32_t val)
{
    ASM("out %0,%1" : : "a"(val), "Nd"(port) : "memory");
}

/* Read a single byte from a given I/O port address. */
static ALWAYS_INLINE uint8_t inb(uint16_t port)
{
    uint8_t val;
    ASM("in %1, %0" : "=a"(val) : "Nd"(port) : "memory");
    return val;
}

/* Read 2 bytes from a given I/O port address. */
static ALWAYS_INLINE uint16_t inw(uint16_t port)
{
    uint16_t val;
    ASM("in %1, %0" : "=a"(val) : "Nd"(port) : "memory");
    return val;
}

/* Read 4 bytes from a given I/O port address. */
static ALWAYS_INLINE uint32_t inl(uint16_t port)
{
    uint32_t val;
    ASM("in %1,%0" : "=a"(val) : "Nd"(port) : "memory");
    return val;
}

static ALWAYS_INLINE void hlt(void)
{
    ASM("hlt");
}

static inline void
cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
    ASM("cpuid" : "=a"(*eax), "=b"(*ebx), "=c"(*ecx),
        "=d"(*edx) : "a"(leaf) : "memory");
}

/*
 * Define quick helper functions for CPUID calls that only need to access one
 * of the result registers.
 */
#define CPUID_FUNCTION(_reg)                           \
    static inline uint32_t cpuid_##_reg(uint32_t leaf) \
    {                                                  \
        uint32_t eax;                                  \
        uint32_t ebx;                                  \
        uint32_t ecx;                                  \
        uint32_t edx;                                  \
                                                       \
        cpuid(leaf, &eax, &ebx, &ecx, &edx);           \
        return _reg;                                   \
    }

CPUID_FUNCTION(eax)
CPUID_FUNCTION(ebx)
CPUID_FUNCTION(ecx)
CPUID_FUNCTION(edx)

#undef CPUID_FUNCTION

/*
 * @return Whether the CPU supports model specific registers (MSR).
 * This should only be the case with old hardware.
 */
static inline bool cpu_has_msr(void)
{
    return (cpuid_edx(CPUID_LEAF_GETFEATURES) >> 5) & 0x1;
}

/* Read from specific register */
static inline uint64_t rdmsr(uint32_t msr)
{
    uint32_t eax;
    uint32_t edx;
    ASM("rdmsr" : "=a"(eax), "=d"(edx) : "c"(msr));
    return (((uint64_t)edx) << 32) | eax;
}

/* Write into model specific register */
static inline void wrmsr(uint32_t msr, uint64_t val)
{
    uint32_t eax = val;
    uint32_t edx = val >> 32;
    ASM("wrmsr" : : "a"(eax), "d"(edx), "c"(msr));
}
#endif /* KERNEL_I686_UTILS_CPU_OPS_H */
