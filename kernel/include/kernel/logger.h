/**
 * Logger library for our kernel.
 *
 * When using the logger, one must specify a domain
 * used as a prefix to the message.
 *
 * For example, when logging an error related to serial devices,
 * the caller could call the logger the following way:
 *
 *   log_err("serial", "error message goes here");
 */
#ifndef KERNEL_LOGGER_H
#define KERNEL_LOGGER_H

#define LOG_FMT_8 "0x%hhx"
#define LOG_FMT_16 "0x%hx"
#define LOG_FMT_32 "0x%x"
#define LOG_FMT_64 "0x%llx"

#define ANSI_RED "\033[31"
#define ANSI_YELLOW "\033[33"
#define ANSI_CYAN "\033[36"
#define ANSI_DEFAULT "\033[39"
#define ANSI_RESET "\033[0m"

/**
 * \brief Print a log message onto the terminal.
 *
 * All logging functions will prefix the message with the
 * specified domain name. The latter will change color
 * depending on the logging level.
 *
 * \param type The prefixed log type
 * \param domain Used as a prefix to the error message.
 * \param msg The actual message
 */
void log(const char *type, const char *domain, const char *msg, ...);

#define log_err(...) log(ANSI_RED ";1;4mERROR" ANSI_RESET " ", __VA_ARGS__)
#define log_warn(...) log(ANSI_YELLOW ";1mWARN" ANSI_RESET "  ", __VA_ARGS__)
#define log_dbg(...) log(ANSI_CYAN "mDEBUG" ANSI_RESET " ", __VA_ARGS__)
#define log_info(...) log(ANSI_DEFAULT "mINFO" ANSI_RESET "  ", __VA_ARGS__)

#endif /* KERNEL_LOGGER_H */
