#include <kernel/interrupts.h>
#include <kernel/logger.h>
#include <kernel/printk.h>
#include <kernel/terminal.h>

#include <stdarg.h>

void log(const char *type, const char *domain, const char *msg, ...)
{
    va_list parameters;
    va_start(parameters, msg);
    log_vlog(type, domain, msg, parameters);
    va_end(parameters);
}

void log_vlog(const char *type, const char *domain, const char *msg,
              va_list parameters)
{
    printk("%s%s" ANSI_RESET " \t\033[0;2m", type, domain);
    vprintk(msg, parameters);
    printk(ANSI_RESET "\n");
}
