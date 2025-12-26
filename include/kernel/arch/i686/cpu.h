#ifndef KERNEL_ARCH_I686_UTILS_CPU_OPS_H
#define KERNEL_ARCH_I686_UTILS_CPU_OPS_H

#include <kernel/types.h>

#include <utils/bits.h>
#include <utils/compiler.h>
#include <utils/map.h>

#define CPU_CACHE_ALIGN 64 /* 64B L1 cache lines */

#define X86_FEATURE_WORDS 2 /* Number of CPUID leaves that contain features. */

struct x86_cpuinfo {
    const char *vendor;
    u32 features[X86_FEATURE_WORDS];
};

extern struct x86_cpuinfo cpuinfo;

/*
 * Register read/write wrappers.
 */

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

/*
 * CPU control registers.
 */

#define CR0_PG BIT(31) /* Paging enable */
#define CR0_CD BIT(30) /* Cache disable */
#define CR0_NW BIT(29) /* Not write-through */

#define CR4_PAE BIT(5) /* PAE paging enable */

/*
 * ASM instruction wrappers.
 */

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

static ALWAYS_INLINE void insb(uint16_t port, uint8_t *buffer, size_t size)
{
    size_t count = size / sizeof(*buffer);

    asm volatile("cld; rep insb"
                 : "+D"(buffer), "+c"(count)
                 : "d"(port)
                 : "memory");
}

static ALWAYS_INLINE void insw(uint16_t port, uint16_t *buffer, size_t size)
{
    size_t count = size / sizeof(*buffer);

    asm volatile("cld; rep insw"
                 : "+D"(buffer), "+c"(count)
                 : "d"(port)
                 : "memory");
}

static ALWAYS_INLINE void insl(uint16_t port, uint32_t *buffer, size_t size)
{
    size_t count = size / sizeof(*buffer);

    asm volatile("cld; rep insl"
                 : "+D"(buffer), "+c"(count)
                 : "d"(port)
                 : "memory");
}

#include <cpuid.h> /* provided by GCC */

#define cpuid(leaf, eax, ebx, ecx, edx) __get_cpuid(leaf, eax, ebx, ecx, edx)

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

#define CPUID_LEAF_GETVENDOR 0
#define CPUID_LEAF_GETFEATURES 1
#define CPUID_LEAF_GETFEATURES_EXT 7

/* Vendor codes used by popular hypervisors. */
#define signature_QEMU_ebx 0x47435443          // [TCGT]CGTCGTCG
#define signature_KVM_ebx 0x4D564B20           // [ KVM]KVMKVM
#define signature_VMWARE_ebx 0x61774D56        // [VMwa]reVMware
#define signature_VIRTUALBOX_ebx 0x786F4256    // [VBox]VBoxVBox
#define signature_XEN_ebx 0x566E6558           // [XenV]MMXenVMM
#define signature_HYPERV_ebx 0x7263694D        // [Micr]osoft Hv
#define signature_PARALLELS_ebx 0x6C727020     // [ prl] hyperv
#define signature_PARALLELS_ALT_ebx 0x6570726C // [lrpe]pyh vr
#define signature_BHYVE_ebx 0x76796862         // [bhyv]e bhyve
#define signature_QNX_ebx 0x20584E51           // [ QNX]QVMBSQG

#define X86_FEATURES(F) \
    \
    /* Features in %ecx for leaf 1 */ \
    F(SSE3, 0, 0), \
    F(PCLMUL, 0, 1), \
    F(DTES64, 0, 2), \
    F(MONITOR, 0, 3), \
    F(DSCPL, 0, 4), \
    F(VMX, 0, 5), \
    F(SMX, 0, 6), \
    F(EIST, 0, 7), \
    F(TM2, 0, 8), \
    F(SSSE3, 0, 9), \
    F(CNXTID, 0, 10), \
    F(FMA, 0, 12), \
    F(CMPXCHG16B, 0, 13), \
    F(xTPR, 0, 14), \
    F(PDCM, 0, 15), \
    F(PCID, 0, 17), \
    F(DCA, 0, 18), \
    F(SSE41, 0, 19), \
    F(SSE42, 0, 20), \
    F(x2APIC, 0, 21), \
    F(MOVBE, 0, 22), \
    F(POPCNT, 0, 23), \
    F(TSCDeadline, 0, 24), \
    F(AES, 0, 25), \
    F(XSAVE, 0, 26), \
    F(OSXSAVE, 0, 27), \
    F(AVX, 0, 28), \
    F(F16C, 0, 29), \
    F(RDRND, 0, 30), \
    \
    /* Features in %edx for leaf 1 */ \
    F(FPU, 1, 0), \
    F(VME, 1, 1), \
    F(DE, 1, 2), \
    F(PSE, 1, 3), \
    F(TSC, 1, 4), \
    F(MSR, 1, 5), \
    F(PAE, 1, 6), \
    F(MCE, 1, 7), \
    F(CMPXCHG8B, 1, 8), \
    F(APIC, 1, 9), \
    F(SEP, 1, 11), \
    F(MTRR, 1, 12), \
    F(PGE, 1, 13), \
    F(MCA, 1, 14), \
    F(CMOV, 1, 15), \
    F(PAT, 1, 16), \
    F(PSE36, 1, 17), \
    F(PSN, 1, 18), \
    F(CLFSH, 1, 19), \
    F(DS, 1, 21), \
    F(ACPI, 1, 22), \
    F(MMX, 1, 23), \
    F(FXSAVE, 1, 24), \
    F(SSE, 1, 25), \
    F(SSE2, 1, 26), \
    F(SS, 1, 27), \
    F(HTT, 1, 28), \
    F(TM, 1, 29), \
    F(PBE, 1, 31), \

#define X86_FEATURE_NAME(_feature) X86_FEATURE_##_feature
#define X86_FEATURE_VAL(_word, _bit) ((_word << X86_FEATURE_WORD_OFF) | (_bit & 0xff))
#define X86_FEATURE_WORD_OFF 8

enum x86_cpu_feature {
#define DEFINE_X86_FEATURE(_name, _word, _bit) \
    X86_FEATURE_NAME(_name) = X86_FEATURE_VAL(_word, _bit)
X86_FEATURES(DEFINE_X86_FEATURE)
#undef DEFINE_X86_FEATURE
};

static inline bool cpu_test_feature(enum x86_cpu_feature feature)
{
    int leaf = (feature >> X86_FEATURE_WORD_OFF);
    int bit = feature & (BIT(X86_FEATURE_WORD_OFF) - 1);

    return BIT_READ(cpuinfo.features[leaf], bit);
}

#define cpu_has_feature(_feature) cpu_test_feature(X86_FEATURE_NAME(_feature))

enum x86_msr {
    MSR_PAT = 0x277,
};

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
