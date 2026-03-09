#include <kernel/interrupts.h>
#include <kernel/syscalls.h>

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

void arch_syscall_set_return_value(interrupt_frame *frame, u32 value)
{
    frame->stub.eax = value;
}
