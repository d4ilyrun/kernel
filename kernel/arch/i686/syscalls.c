#include <kernel/interrupts.h>
#include <kernel/process.h>
#include <kernel/syscalls.h>

/*
 *
 */
void arch_syscall_get_args(interrupt_frame *frame, syscall_args_t *args)
{
    args->nr = frame->stub.eax;
    args->arg1 = frame->stub.ebx;
    args->arg2 = frame->stub.ecx;
    args->arg3 = frame->stub.edx;
    args->arg4 = frame->stub.esi;
    args->arg5 = frame->stub.edi;
    args->arg6 = frame->stub.ebp;
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
    frame->stub.eax = value;
}
