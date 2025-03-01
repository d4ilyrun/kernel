.global arch_process_switch
.global __arch_process_jump_to_userland

/* extern process_t current_process */
.extern current_process

/* extern tss_t kernel_tss */
.extern kernel_tss

.section .text

/* void arch_process_switch(struct x86_process *) */
.type arch_process_switch, @function
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
    movl %esp, 0xc(%esi)

    // Load next process
    movl %edi, current_process
    movl 0xc(%edi), %esp    // switch stack
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


/* void __arch_process_jump_to_userland(u32 entrypoint, u32 cs, u32 ds, u32 esp) */
.type __arch_process_jump_to_userland, @function
__arch_process_jump_to_userland:

    // discard EIP, we will not be returning to kernel mode anyway
    addl $4, %esp

    popl %eax  // eax = entrypoint
    popl %ebx  // ebx = cs
    popl %ecx  // ecx = ds
    popl %edx  // edx = esp

    // setup data segments for userland
    movl %ecx, %ds
    movl %ecx, %es
    movl %ecx, %fs
    movl %ecx, %gs

    // When switching to another privilege level, the following values are popped and updated
    // from the stack by the cpu: eip -> cs -> eflags -> esp -> ss
    pushl %ecx      // ss
    pushl %edx      // esp
    pushl $0x202    // eflags: IE
    pushl %ebx      // cs
    pushl %eax      // eip

    // jump to userland !
    iret
