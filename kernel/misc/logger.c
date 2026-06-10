#include <kernel/interrupts.h>
#include <kernel/logger.h>
#include <kernel/console.h>
#include <kernel/printk.h>
#include <kernel/terminal.h>

#include <string.h>
#include <stdarg.h>

/** Format used for displaying logs of a given level */
struct log_level_format {
    const char *name;  /** This log level's name */
    enum console_color fg_color;
};

static const struct log_level_format log_level[LOG_LEVEL_COUNT] = {
    [LOG_LEVEL_ERR] =   { "ERROR ", COLOR_BOLD_RED },
    [LOG_LEVEL_WARN] =  { "WARN  ", COLOR_BOLD_YELLOW },
    [LOG_LEVEL_INFO] =  { "INFO  ", COLOR_WHITE },
    [LOG_LEVEL_DEBUG] = { "DEBUG ", COLOR_CYAN },
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
        console_write_string("[");
        if (log_level[level].fg_color)
            console_set_fg_color(log_level[level].fg_color);
        console_write(domain, strlen(domain));
        if (log_level[level].fg_color)
            console_set_fg_color(COLOR_NONE);
        console_write_string("] ");
    }

    vprintk(format, parameters);
    printk("\n");
}
