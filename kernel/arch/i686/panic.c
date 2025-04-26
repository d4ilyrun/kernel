#include <kernel/cpu.h>
#include <kernel/interrupts.h>
#include <kernel/logger.h>
#include <kernel/memory.h>
#include <kernel/printk.h>
#include <kernel/process.h>
#include <kernel/symbols.h>

#include <kernel/arch/i686/gdt.h>

#include <utils/macro.h>

#include <stdarg.h>
#include <stddef.h>

#define KERNEL_PANIC_STACK_DUMP_SIZE 64

struct stackframe_t {
    struct stackframe_t *ebp;
    u32 eip;
};

#undef LOG_DOMAIN

void __stack_trace(struct stackframe_t *frame)
{
    if (frame == NULL) {
        return;
    }

    printk("Call stack:\n");
    for (int i = 0; frame != NULL; ++i, frame = frame->ebp) {

        // Avoid infinite unwiding when stackframe is invalid
        if (!IN_RANGE((vaddr_t)frame->eip, KERNEL_CODE_START, KERNEL_CODE_END))
            break;

        // We substract the size of the call instruction to avoid getting an
        // invalid symbol when at the ned of a function marked with the
        // attribute noreturn
        const kernel_symbol_t *symbol = kernel_symbol_from_address(frame->eip -
                                                                   sizeof(u16));
        printk("  #%d  " FMT32 " in <%s%+d>\n", i, frame->eip,
               kernel_symbol_name(symbol), frame->eip - symbol->address);
    }
    printk("===\n");
}

void stack_trace(void)
{
    __stack_trace(__builtin_frame_address(0));
}

#define LOG_DOMAIN "PROC"
static void panic_dump_process(void)
{
    log_err("%s (TID: %d)", current->process->name, current->tid);
    log_err("ESP0=" FMT32 " ESP=" FMT32 " CR3=" FMT32, current->context.esp0,
            current->context.esp_user, current->context.cr3);
}

#undef LOG_DOMAIN

#define LOG_DOMAIN "REGS"
static void panic_dump_registers(void)
{
    // We only dump useful registers, or else it will become unreadable
    // Registers deemed "useful" are subject to change in the future

    log_err("Summary of registers");
    log_err("CR0=" FMT32 " CR2=" FMT32 " CR3=" FMT32, read_cr0(), read_cr2(),
            read_cr3());
    log_err("CS=" FMT16 " SS=" FMT16, read_cs(), read_ss());
}
#undef LOG_DOMAIN

#define LOG_DOMAIN "TRACE"
static void panic_stack_trace(void)
{
    // Stack frame at this point:
    // #0 - panic
    // #1 - <faulty_function>
    // #2..#n - preceding function calls
    struct stackframe_t *frame = __builtin_frame_address(0);

    if (frame == NULL) {
        log_err("Corrupted stack frame.");
        return;
    }

    // remove call to <panic> from the stack frame
    __stack_trace(frame->ebp);
}
#undef LOG_DOMAIN

#define LOG_DOMAIN "STACK"
static void panic_dump_stack(u32 esp, u32 size)
{
    log_err("** start of stack: at esp=" FMT32 " **", esp);

    for (u32 offset = 0; offset < size; offset += sizeof(u32)) {
        log_err("esp+%-3d: " FMT32, offset, *(volatile u32 *)(esp + offset));
    }

    log_err("** end of stack **");
}
#undef LOG_DOMAIN

void panic(u32 esp, const char *msg, ...)
{
    interrupts_disable();

    va_list parameters;
    va_start(parameters, msg);

    printk("\n\033[31;1;4m!!! KERNEL PANIC !!!" ANSI_RESET "\033[31;1m\n\n");
    vprintk(msg, parameters);
    printk(ANSI_RESET "\n\n");

    va_end(parameters);

    panic_dump_process();
    printk("\n");

    panic_dump_registers();
    printk("\n");

    panic_dump_stack(esp, KERNEL_PANIC_STACK_DUMP_SIZE);
    printk("\n");

    gdt_log();
    printk("\n");

    panic_stack_trace();
    printk("\n");

    // Halt the kernel's execution

halt:
    ASM("hlt");
    goto halt;
}
