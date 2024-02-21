extern default_interrupt_handler

%macro interrupt 2
    section .text
    align 4
__%2_stub_handler:
    cli         ; disable interrupts
    pushad
    push $0     ; push empty error code and interrupt number onto stack
    push %1
    cld
    call default_interrupt_handler
    add esp, 8  ; discard error & interrupt number from stack
    popad
    sti         ; re-enable interrupts
    iret
%endmacro

%macro interrupt_error 2
    section .text
    align 4
__%2_stub_handler:
    cli         ; disable interrupts
    pushad
    push %1     ; push interrupt number onto stack
    cld
    call default_interrupt_handler
    add esp, 8  ; discard error & interrupt number from stack
    popad
    sti         ; re-enable interrupts
    iret
%endmacro

; Define all the interrupt handler stubs for the internal interrupts
interrupt 0, division_error
interrupt 1, debug
interrupt 2, nmi
interrupt 3, breakpoint
interrupt 4, overlfow
interrupt 5, bound_range
interrupt 6, invalid_opcode
interrupt 7, device_not_available
interrupt_error 8, double_fault
interrupt 9, segment_overrun
interrupt_error 10, invalid_tss
interrupt_error 11, segment_not_present
interrupt_error 12, stack_segment_fault
interrupt_error 13, general_protection
interrupt_error 14, page_fault
interrupt 15, intel_reserved
interrupt 16, fpu_error
interrupt_error 17, alignment_check
interrupt 18, machine_check
interrupt 19, smid_fp_exception
interrupt 20, virtualization
interrupt_error 21, control_protection

%macro stub 1
    dd __%1_stub_handler
%endmacro

; addressable interrupt handler stubs table
global interrupt_handler_stubs

interrupt_handler_stubs:
  stub division_error
  stub debug
  stub nmi
  stub breakpoint
  stub overlfow
  stub bound_range
  stub invalid_opcode
  stub device_not_available
  stub double_fault
  stub segment_overrun
  stub invalid_tss
  stub segment_not_present
  stub stack_segment_fault
  stub general_protection
  stub page_fault
  stub intel_reserved
  stub fpu_error
  stub alignment_check
  stub machine_check
  stub smid_fp_exception
  stub virtualization
  stub control_protection
