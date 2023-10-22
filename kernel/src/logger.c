#include <kernel/logger.h>
#include <kernel/terminal.h>

#define LOGGER_BG VGA_COLOR_BLACK
#define LOGGER_FG VGA_COLOR_LIGHT_GREY

#define LOGGER_FG_ERR VGA_COLOR_RED
#define LOGGER_FG_WARN VGA_COLOR_BROWN
#define LOGGER_FG_DBG VGA_COLOR_LIGHT_BROWN
#define LOGGER_FG_INFO VGA_COLOR_LIGHT_GREY

static inline void log_log(uint8_t fg, const char *domain, const char *msg)
{
    tty_set_color(vga_entry_color(fg, LOGGER_BG));
    tty_puts(domain);
    tty_set_color(vga_entry_color(LOGGER_FG, LOGGER_BG));
    tty_puts(": ");
    tty_puts(msg);
    tty_putchar('\r');
}

void log_err(const char *domain, const char *msg)
{
    log_log(LOGGER_FG_ERR, domain, msg);
}

void log_warn(const char *domain, const char *msg)
{
    log_log(LOGGER_FG_WARN, domain, msg);
}

void log_dbg(const char *domain, const char *msg)
{
    log_log(LOGGER_FG_DBG, domain, msg);
}

void log_info(const char *domain, const char *msg)
{
    log_log(LOGGER_FG_INFO, domain, msg);
}
