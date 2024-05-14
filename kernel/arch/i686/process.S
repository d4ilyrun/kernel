/* extern process_t current_process */
.extern current_process

/* extern tss_t kernel_tss */
.extern kernel_tss

.section .text
.global arch_process_switch

/* void arch_process_switch(struct x86_process *) */
arch_process_switch:

    // retrieve first argument
    mov 4(%esp), %edi

    // Save current process
    // Save callee registers to be popped when re-entering this process
    pushl %edi
    pushl %esi
    pushl %ebx
    pushl %ebp
    // Save current state into current process: cr3, esp
    movl (current_process), %esi
    movl %cr3, %ebx
    movl %ebx, 0x0(%esi)
    movl %esp, 0x8(%esi)

    // Load next process
    movl %edi, current_process
    movl 0x8(%edi), %esp    // switch stack
    movl 0x0(%edi), %eax    // eax=cr3
    movl 0x4(%edi), %edi    // edi=esp0
    // write kernel stack into TSS.ESP0
    leal (kernel_tss), %esi
    movl %edi, 0x4(%esi)
    // Load page directory only if necessary
    cmpl %eax, %ebx
    je 1f
    movl %eax, %cr3
1:
    // Start next process
    // Pop callee saved registers
    popl %ebp
    popl %ebx
    popl %esi
    popl %edi
    // Jump back into the process using the EIP pushed at EBP by the caller
    ret
