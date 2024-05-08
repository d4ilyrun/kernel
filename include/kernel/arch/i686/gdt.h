/**
 * @defgroup gdt Global Descriptor Table
 * @ingroup x86
 *
 * @{
 *
 * # Global Descriptor Table
 *
 * The GDT is responsible for segmenting the physical address space into
 * different parts with different level of security each.
 *
 * The segments can contain either code or data.
 *
 * Each "segment" can then be accessed using a selector.
 * These selectors are the ones stored inside the segement registers (cs, ds ..)
 * When addressing a linear address (~physical), the processor infact
 * dereferences an offset inside the related segment's address range.
 *
 * @warning This is a x86 structure only
 *
 * @see https://wiki.osdev.org/Global_Descriptor_Table
 */

#ifndef KERNEL_ARCH_I686_GDT_H
#define KERNEL_ARCH_I686_GDT_H

#include <utils/compiler.h>
#include <utils/types.h>

// Hard coded constant linear base 32-bits address for the TSS
#define GDT_TSS_BASE_ADDRESS 0x00000000UL

/**
 * @brief Known fixed indexes inside the GDT
 */
enum {
    GDT_ENTRY_NULL = 0,        /** Required NULL segment */
    GDT_ENTRY_KERNEL_CODE = 1, /** Kernel code segment */
    GDT_ENTRY_KERNEL_DATA = 2, /** Kernel data segment */
    GDT_ENTRY_USER_CODE = 3,   /** User code segment */
    GDT_ENTRY_USER_DATA = 4,   /** User data segment */
    GDT_ENTRY_TSS = 5
};

/**
 * @struct gdtr GDT register
 * The GDT is pointed to by the value in the GDTR register.
 */
typedef struct gdtr gdtr;
struct PACKED gdtr {
    u16 size;
    u32 offset;
};

/**
 * @struct gdt_descriptor
 * A single entry inside the GDT.
 */
typedef struct gdt_descriptor {
    u32 base;
    u32 limit : 20;
    u8 access;
    u8 flags : 4;
} gdt_descriptor;

/**
 * @struct gdt_tss Task State Segment
 *
 * A Task State Segment (TSS) is a binary data structure specific to the IA-32
 * and x86-64 architectures.
 * It holds information about a task, namely registers, for when switching
 * tasks.
 *
 * @see https://wiki.osdev.org/Task_State_Segment
 */
typedef struct PACKED gdt_tss {
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
} gdt_tss;

/**
 * @union segment_selector
 * @brief Identifies a segment inside the GDT or LDT.
 * @see https://wiki.osdev.org/Segment_Selector
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
 * @brief Initialize the GDTR and GDT.
 *
 * - Load the GDT's base address int GDTR
 * - Setup the NULL segment at offset 0
 * - Add our global segment descriptors (ring 0)
 */
void gdt_init(void);

/**
 * @brief Load a segment descriptor into the Global Descriptor Table.
 *
 * @param segment The segment's content
 * @param index The segment's index within the GDT.
 */
void gdt_load_segment(gdt_descriptor, u16 index);

/** Print the content of the GDT and GDTR. */
void gdt_log(void);

#endif /* KERNEL_ARCH_I686_GDT_H */
