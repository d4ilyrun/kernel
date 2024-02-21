#include <kernel/interrupts.h>
#include <kernel/logger.h>

#include <kernel/i686/gdt.h>

#include <utils/compiler.h>
#include <utils/macro.h>

#include <string.h>

// Assembly functions defined inside 'asm/gdt.S'
void reload_segment_registers(void);

#define GDT_ENTRY_SIZE 8
#define GDT_SIZE (256 * GDT_ENTRY_SIZE)
#define GDT_LENGTH (GDT_SIZE / sizeof g_global_segments[0])

static gdt_descriptor g_global_segments[] = {
    {0, 0, 0, 0},            // **required** NULL segment
    {0, 0xFFFFF, 0x9A, 0xC}, // Kernel Mode code segment
    {0, 0xFFFFF, 0x92, 0xC}, // Kernel Mode data segment
    {0, 0xFFFFF, 0xFA, 0xC}, // User Mode code segment
    {0, 0xFFFFF, 0xF2, 0xC}, // User Mode data segment
    {GDT_TSS_BASE_ADDRESS, sizeof(gdt_tss), 0x80, 0x0}, // Task State segment
};

void gdt_init(void)
{
    interrupts_disable();

    /**
     * Initialize the content of the GDTR register.
     *
     * To perform this operation, we assume that we are in protected mode,
     * and that we are using a falt model (which is the case if using GRUB).
     *
     * FIXME: Should not use a fixed address
     */
    static gdtr gdtr = {.size = GDT_SIZE - 1, .offset = GDT_BASE_ADDRESS};
    ASM("lgdt (%0)" : : "m"(gdtr) : "memory");

    /** Load a NULL sector at index 0 */
    memset((void *)GDT_BASE_ADDRESS, 0, GDT_ENTRY_SIZE);

    // Load all default segments into the GDT
    for (u16 segment = 1;
         segment < (sizeof g_global_segments / sizeof(gdt_descriptor));
         segment++) {
        gdt_load_segment(g_global_segments[segment], segment);
    }

    reload_segment_registers();
}

void gdt_load_segment(gdt_descriptor segment, u16 index)
{
    if (!BETWEEN(index, 0, GDT_LENGTH)) {
        log_err("GDT", "Cannot insert: Invalid index");
        return;
    }

    u8 *gdt = (u8 *)(GDT_BASE_ADDRESS + (index * GDT_ENTRY_SIZE));

    log_dbg("GDT", "Loading segment descriptor");
    gdt[0] = LSB(segment.limit);
    gdt[1] = MSB(segment.limit);
    gdt[2] = LSB(segment.base);
    gdt[3] = MSB(segment.base);
    gdt[4] = LSB(segment.base >> 16);
    gdt[5] = segment.access;
    gdt[6] = ((segment.limit >> 16) & 0xF) | (segment.flags << 4);
    gdt[7] = MSB(segment.base >> 16);
}

void gdt_log(void)
{
    // Print the content of the GDTR
    gdtr gdtr;
    ASM("sgdt %0" : "=m"(gdtr) : : "memory");
    log_info("GDT", "GDTR = { size: " LOG_FMT_16 ", offset: " LOG_FMT_32 "}",
             gdtr.size, gdtr.offset);

    // Print each global segment
    // We don't support adding sectors manually for now so we are good
    // printing those only.
    log_info("GDT", "Global segment descriptors");

    for (u16 index = 0;
         index < sizeof(g_global_segments) / sizeof(gdt_descriptor); ++index) {

        // Load segment from index
        u8 *segment = (u8 *)(GDT_BASE_ADDRESS + (index * GDT_ENTRY_SIZE));

        printf("%hd = { base: " LOG_FMT_32 ", limit: " LOG_FMT_32
               ", access: " LOG_FMT_8 ", flags: " LOG_FMT_8 " }\n",
               index,
               /* base */
               segment[2] | segment[3] << 8 | segment[4] << 16 |
                   segment[7] << 24,
               /* limit */
               segment[0] | (segment[1] << 8) | (segment[6] & 0xF) << 16,
               segment[5], (segment[6] & 0xF0) >> 4);
    }
}
