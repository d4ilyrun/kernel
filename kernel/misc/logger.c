#include <kernel/interrupts.h>
#include <kernel/logger.h>
#include <kernel/printk.h>
#include <kernel/terminal.h>

#include <stdarg.h>

#define LOG_DOMAIN_SEPARATOR " "

/** Format used for displaying logs of a given level */
struct log_level_format {
    const char *color; /** Color used for the level's name */
    const char *name;  /** This log level's name */
};

static const struct log_level_format log_level[LOG_LEVEL_COUNT] = {
    [LOG_LEVEL_ERR] = {"\033[31;1;1m", "ERROR "},
    [LOG_LEVEL_WARN] = {"\033[33;1m", "WARN  "},
    [LOG_LEVEL_INFO] = {"\033[39m", "INFO  "},
    [LOG_LEVEL_DEBUG] = {"\033[36m", "DEBUG "},
};

static enum log_level max_log_level = LOG_LEVEL_ALL;

void log_set_level(enum log_level level)
{
    max_log_level = level;
}

void log(enum log_level level, const char *domain, const char *msg, ...)
{
    va_list parameters;

    va_start(parameters, msg);
    log_vlog(level, domain, msg, parameters);
    va_end(parameters);
}

void log_vlog(enum log_level level, const char *domain, const char *format,
              va_list parameters)
{
    if (level > max_log_level)
        return;

    if (domain) {
        if (log_level[level].color)
            printk("[%s%s%s%s]" LOG_DOMAIN_SEPARATOR, ANSI_RESET,
                   log_level[level].color, domain, ANSI_RESET);
        else
            printk("[%s%s]" LOG_DOMAIN_SEPARATOR, ANSI_RESET, domain);
    }

    vprintk(format, parameters);
    printk("\n");
}
