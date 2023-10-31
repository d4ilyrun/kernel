#include <kernel/logger.h>
#include <kernel/terminal.h>

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
