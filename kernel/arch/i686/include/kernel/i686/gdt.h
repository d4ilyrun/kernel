/**
 * x86 Global Descriptor Table
 *
 * @see @ref https://wiki.osdev.org/Global_Descriptor_Table GDT
 */

#ifndef KERNEL_ARCH_I686_GDT_H
#define KERNEL_ARCH_I686_GDT_H

#include <utils/compiler.h>
#include <utils/types.h>

// 32-bit linear base address of the GDT (protected mode).
// Should be put inside GDTR during startup.
#define GDT_BASE_ADDRESS 0x00000800UL

// Hard coded constant linear base 32-bits address for the TSS
#define GDT_TSS_BASE_ADDRESS 0x00000000UL

///< Known fixed indexes inside the GDT
#define GDT_ENTRY_NULL 0
#define GDT_ENTRY_KERNEL_CODE 1
#define GDT_ENTRY_KERNEL_DATA 2
#define GDT_ENTRY_USER_CODE 3
#define GDT_ENTRY_USER_DATA 4
#define GDT_ENTRY_TSS 5

/**
 * \struct GDT register
 * The GDT is pointed to by the value in the GDTR register.
 */
typedef struct gdtr gdtr;
struct PACKED gdtr {
    u16 size;
    u32 offset;
};

/**
 * \struct gdt_descriptor
 *
 * A single entry inside the GDT.
 */
typedef struct gdt_descriptor gdt_descriptor;
struct gdt_descriptor {
    u32 base;
    u32 limit : 20;
    u8 access;
    u8 flags : 4;
};

/**
 * @struct Task State Segment
 *
 * A Task State Segment (TSS) is a binary data structure specific to the IA-32
 * and x86-64 architectures.
 * It holds information about a task, namely registers, for when switching
 * tasks.
 *
 * @ref https://wiki.osdev.org/Task_State_Segment TSS
 */
typedef struct gdt_tss gdt_tss;
struct PACKED gdt_tss {
    u16 link;
    u16 _reserved1;
    u32 esp0;
    u16 ss0;
    u16 _reserved2;
    u32 esp1;
    u16 ss1;
    u16 _reserved3;
    u32 esp2;
    u16 ss2;
    u16 _reserved4;
    u32 cr3;
    u32 eip;
    u32 eflags;
    u32 eax;
    u32 ecx;
    u32 edx;
    u32 ebx;
    u32 esp;
    u32 ebp;
    u32 esi;
    u32 edi;
    u16 es;
    u16 _reserved5;
    u16 cs;
    u16 _reserved6;
    u16 ss;
    u16 _reserved7;
    u16 ds;
    u16 _reserved8;
    u16 fs;
    u16 _reserved9;
    u16 gs;
    u16 _reserved10;
    u16 ldtr;
    u16 _reserved11;
    u16 _reserved12;
    u16 iopb;
    u32 ssp;
};

/**
 * Identifies a segment inside the GDT or LDT.
 * @link https://wiki.osdev.org/Segment_Selector
 */
typedef union {
    u16 raw;
    struct {
        u8 rpl : 2;
        u8 ti : 1;
        u16 index : 13;
    } PACKED;
} segment_selector;

/**
 * \brief Initialize the GDTR and GDT.
 *
 * - Load the GDT's base address int GDTR
 * - Setup the NULL segment at offset 0
 * - Add our global segment descriptors (ring 0)
 */
void gdt_init(void);

/**
 * Load a segment descriptor into the Global Descriptor Table.
 *
 * @param segment The segment's value
 * @param index The segment's index within the GDT.
 */
void gdt_load_segment(gdt_descriptor, u16 index);

/** Print the content of the GDT and GDTR. */
void gdt_log(void);

#endif /* KERNEL_ARCH_I686_GDT_H */
