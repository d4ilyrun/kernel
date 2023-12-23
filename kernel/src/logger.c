#include <kernel/logger.h>
#include <kernel/terminal.h>
#include <kernel/interrupts.h>

#include <stdarg.h>
#include <stdio.h>

void log(const char *type, const char *domain, const char *msg, ...)
{
    va_list parameters;
    va_start(parameters, msg);

    printf("%s%s" ANSI_RESET " \t\033[0;2m", type, domain);
    vprintf(msg, parameters);
    printf(ANSI_RESET "\n");

    va_end(parameters);
}

void panic(const char *msg, ...)
{
    interrupts_disable();

    va_list parameters;
    va_start(parameters, msg);

    printf("\n\033[31;1;4m!!! KERNEL PANIC !!!" ANSI_RESET "\033[31;1m\n\n");
    vprintf(msg, parameters);
    printf(ANSI_RESET "\n");

    va_end(parameters);

    // TODO: Dump the kernel's state
    // This includes:
    // * Registers

    // Halt the kernel's execution

halt:
    ASM("hlt");
    goto halt;
}
