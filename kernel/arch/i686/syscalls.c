#include <kernel/interrupts.h>
#include <kernel/process.h>
#include <kernel/syscalls.h>

/*
 *
 */
void arch_syscall_get_args(interrupt_frame *frame, syscall_args_t *args)
{
    args->nr = frame->regs.eax;
    args->arg1 = frame->regs.ebx;
    args->arg2 = frame->regs.ecx;
    args->arg3 = frame->regs.edx;
    args->arg4 = frame->regs.esi;
    args->arg5 = frame->regs.edi;
    args->arg6 = frame->regs.ebp;
}

/*
 *
 */
void arch_syscall_set_return_value(u32 value)
{
    struct interrupt_frame *frame;

    /*
     * Update the value inside the **actual** interrupt frame used when
     * returning to userland.
     */
    frame = (void *)current->context.esp0 - sizeof(struct interrupt_frame);
    frame->regs.eax = value;
}
