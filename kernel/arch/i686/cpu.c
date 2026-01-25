#define LOG_DOMAIN "i686"

#include <kernel/cpu.h>
#include <kernel/init.h>
#include <kernel/logger.h>
#include <kernel/memory.h>

#include <utils/macro.h>
#include <utils/math.h>

struct x86_cpuinfo x86_cpuinfo;
const struct cpuinfo *cpuinfo = &x86_cpuinfo.cpuinfo;

/*
 *
 */
void cache_flush(paddr_t addr)
{
    paddr_t cache_line = align_down(addr, cpuinfo->cache_flush_line_size);

    if (!cpuinfo->cache_flush_available) {
        log_warn("cannot flush individual cache lines");
        return;
    }

    if (cpu_has_feature(CLFLUSHOPT)) {
        memory_barrier();
        clflushopt(cache_line);
        memory_barrier();
    } else {
        clflush(cache_line);
    }
}

/*
 *
 */
void cache_flush_range(paddr_t addr, size_t range_size)
{
    paddr_t range_start = align_down(addr, cpuinfo->cache_flush_line_size);
    paddr_t range_end = range_start + range_size;
    bool has_clflush_opt;

    if (!cpuinfo->cache_flush_available) {
        log_warn("cannot flush individual cache lines");
        return;
    }

    if (cpu_has_feature(CLFLUSHOPT)) {
        has_clflush_opt = true;
        memory_barrier();
    } else {
        has_clflush_opt = false;
    }

    for (paddr_t cache_line = range_start; cache_line < range_end;
         cache_line += cpuinfo->cache_flush_line_size) {
        if (has_clflush_opt)
            clflushopt(cache_line);
        else
            clflush(cache_line);
    }

    if (has_clflush_opt)
        memory_barrier();
}

/*
 *
 */
static void cpu_init_caches(struct x86_cpuinfo *cpu)
{
    u32 val;

    /*
     * Enable caching globally.
     *
     * Caching policies can still be selectively configured using page table
     * entries or MTRR registers.
     */
    val = read_cr0();
    val &= ~CR0_CD;
    val &= ~CR0_NW;
    write_cr0(val);

    /*
     * The clflush instructions are not present on older CPUs.
     */
    if (!cpu_has_feature(CLFLUSHOPT) &&
        !cpu_has_feature(CLFSH)) {
        cpu->cpuinfo.cache_flush_available = false;
        log_info("CLFLUSH not available");
    } else {
        val = cpuid_ebx(CPUID_LEAF_GETFEATURES);
        cpu->cpuinfo.cache_flush_line_size = 8 * ((val >> 8) & 0xFF);
        cpu->cpuinfo.cache_flush_available = true;
        log_info("CLFLUSH line size: %dB", cpu->cpuinfo.cache_flush_line_size);
    }
}

struct x86_cpu_vendor {
    const char *vendor;
    u32 ebx;
    u32 ecx;
    u32 edx;
};

#define CPU_VENDOR(_vendor, _name)        \
    {                                     \
        .vendor = _name,                  \
        .ebx = signature_##_vendor##_ebx, \
        .ecx = signature_##_vendor##_ecx, \
        .edx = signature_##_vendor##_edx, \
    }

#define CPU_VENDOR_HV(_vendor, _name)     \
    {                                     \
        .vendor = _name,                  \
        .ebx = signature_##_vendor##_ebx, \
    }

static struct x86_cpu_vendor cpu_vendors[] = {
    CPU_VENDOR(AMD, "AMD"),
    CPU_VENDOR(INTEL, "Intel"),
    CPU_VENDOR_HV(KVM, "KVM"),
    CPU_VENDOR_HV(VMWARE, "VMWare"),
    CPU_VENDOR_HV(VIRTUALBOX, "VirtualBox"),
    CPU_VENDOR_HV(XEN, "Xen"),
    CPU_VENDOR_HV(HYPERV, "Microsoft Hypervisor"),
};

static const char *feature_name[32 * X86_FEATURE_WORDS] = {
#define X86_FEATURE_STRING(_name, _word, _bit) [_word * 32 + _bit] = stringify(_name)
    X86_FEATURES(X86_FEATURE_STRING)
#undef X86_FEATURE_STRING
};

/*
 *
 */
static void cpu_dump_info(enum log_level level, const struct x86_cpuinfo *cpu)
{
    log(level, LOG_DOMAIN, "CPU Information");
    log(level, LOG_DOMAIN, "Vendor: %s", cpu->vendor);

    log(level, LOG_DOMAIN, "Features: ");
    for (int leaf = 0; leaf < X86_FEATURE_WORDS; ++leaf) {
        for (int bit = 0; bit < 32; ++bit) {
            if (cpu_test_feature(X86_FEATURE_VAL(leaf, bit))) {
                if (feature_name[leaf * 32 + bit])
                    printk("%s ", feature_name[leaf * 32 + bit]);
            }
        }
    }
    printk("\n");
}

/*
 *
 */
static void cpu_init_info(struct x86_cpuinfo *cpu)
{
    unsigned int eax;
    unsigned int ebx;
    unsigned int ecx;
    unsigned int edx;

    cpu->vendor = "unknown";

    /* Find vendor information */
    cpuid(CPUID_LEAF_GETVENDOR, &eax, &ebx, &ecx, &edx);
    for (size_t i = 0; i < ARRAY_SIZE(cpu_vendors); ++i) {
        if (cpu_vendors[i].ebx != ebx)
            continue;
        if (cpu_vendors[i].ecx && cpu_vendors[i].ecx != ecx)
            continue;
        if (cpu_vendors[i].edx && cpu_vendors[i].edx != edx)
            continue;
        cpu->vendor = cpu_vendors[i].vendor;
        break;
    }

    cpu->features[0] = cpuid_ecx(CPUID_LEAF_GETFEATURES);
    cpu->features[1] = cpuid_edx(CPUID_LEAF_GETFEATURES);

    cpu_dump_info(LOG_LEVEL_INFO, cpu);
}

/*
 * Initialize the CPU and configure it in a known state.
 */
error_t cpu_init(void)
{
    cpu_init_info(&x86_cpuinfo);
    cpu_init_caches(&x86_cpuinfo);

    return E_SUCCESS;
}

DECLARE_INITCALL(INIT_BOOTSTRAP, cpu_init);
