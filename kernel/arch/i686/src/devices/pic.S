%define PIC_INDEX(_nr) (0x20 + _nr)

extern default_interrupt_handler

%macro irq_handler 2
    section .text
__pic_%2_stub_handler:
    cli         ; disable interrupts
    pushad
    push $0     ; push empty error code and interrupt number onto stack
    push PIC_INDEX(%1)
    cld
    call default_interrupt_handler
    add esp, 8  ; discard error & interrupt number from stack
    popad
    sti         ; re-enable interrupts
    iret
%endmacro

; Define all the interrupt handler stubs for our IRQs
irq_handler 0, timer
irq_handler 1, keyboard
irq_handler 2, cascade
irq_handler 3, com2
irq_handler 4, com1
irq_handler 5, lpt2
irq_handler 6, floppy
irq_handler 7, lpt1
irq_handler 8, cmos
irq_handler 9, free1
irq_handler 10, free2
irq_handler 11, free3
irq_handler 12, ps2
irq_handler 13, fpu
irq_handler 14, ata_primary
irq_handler 15, ata_secondary

%macro stub 1
    dd __pic_%1_stub_handler
%endmacro

; Global, addressable IRQ handler stubs table
global pic_interrupt_handler_stubs

pic_interrupt_handler_stubs:
  stub timer
  stub keyboard
  stub cascade
  stub com2
  stub com1
  stub lpt2
  stub floppy
  stub lpt1
  stub cmos
  stub free1
  stub free2
  stub free3
  stub ps2
  stub fpu
  stub ata_primary
  stub ata_secondary
