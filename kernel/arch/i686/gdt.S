/**
 * Reload all segment registers.
 *
 * This function needs to be called for any changes made to
 * the GDT or GDTR to take effect.
 */

.section .text
.global reload_segment_registers
.type reload_segment_registers, @function

reload_segment_registers:
    // Perform a long jump to force reload CodeSegment register
    jmp $0x08,$.reload_CS // 0x08 == current code segment
.reload_CS:
    // All other segment regsiters only require a mov to be reloaded
    mov %ax, 0x10 // 0x010 = current data segment
    mov %ds, %ax
    mov %es, %ax
    mov %fs, %ax
    mov %gs, %ax
    mov %ss, %ax
    ret
