#include <kernel/interrupts.h>
#include <kernel/logger.h>
#include <kernel/symbols.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

struct stackframe_t {
    struct stackframe_t *ebp;
    u32 eip;
};

static void panic_unwind_stack(void)
{
    // Stack frame at this point:
    // #0 - panic
    // #1 - <faulty_function>
    // #2..#n - preceding function calls
    struct stackframe_t *frame = __builtin_frame_address(0);

    if (frame == NULL) {
        log_err("TRACE", "Corrupted stack frame.");
        return;
    }

    // remove call to <panic> from the stack frame
    frame = frame->ebp;

    for (int i = 0; frame != NULL; ++i, frame = frame->ebp) {

        // We substract the size of the call instruction to avoid getting an
        // invalid symbol when at the ned of a function marked with the
        // attribute noreturn
        const kernel_symbol_t *symbol =
            symbol_from_address(frame->eip - sizeof(u16));
        log_err("TRACE", "#%d  " LOG_FMT_32 " in <%s+%d>", i, frame->eip,
                kernel_symbol_name(symbol), frame->eip - symbol->address);
    }
}

void panic(const char *msg, ...)
{
    interrupts_disable();

    va_list parameters;
    va_start(parameters, msg);

    printf("\n\033[31;1;4m!!! KERNEL PANIC !!!" ANSI_RESET "\033[31;1m\n\n");
    vprintf(msg, parameters);
    printf(ANSI_RESET "\n\n");

    va_end(parameters);

    // TODO: Dump the kernel's state
    // This includes:
    // * Registers

    panic_unwind_stack();
    printf("\n");

    // Halt the kernel's execution

halt:
    ASM("hlt");
    goto halt;
}
