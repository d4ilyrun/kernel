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

#define ANSI_ERR "\033[31;1;4m"
#define ANSI_WARN "\033[33;1m"
#define ANSI_DBG "\033[36m"
#define ANSI_INFO "\033[39m"
#define ANSI_RESET "\033[0m"

#ifndef xSTR
#define xSTR(_x) #_x
#endif

#include <stdio.h>

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

/**
 * \brief Completely stop the kernel's execution
 *
 * This function writes a BOLD RED message to the screen, and completely
 * halts the kernel's execution.
 *
 * This should only be called in case of unrecoverable errors, or asserts
 * that should never be false and would prevent the kernel from functioning
 * as expected.
 *
 * TODO: Dump the kernel's internal state (registers, ...)
 */
void panic(const char *msg, ...) __attribute__((__noreturn__));

// TODO: LOG_LEVEL filter

#define log_err(domain, ...) \
    log(ANSI_ERR "ERROR" ANSI_RESET " ", domain, __VA_ARGS__)
#define log_warn(domain, ...) \
    log(ANSI_WARN "WARN" ANSI_RESET "  ", domain, __VA_ARGS__)
#define log_dbg(domain, ...) \
    log(ANSI_DBG "DEBUG" ANSI_RESET " ", domain, __VA_ARGS__)
#define log_info(domain, ...) \
    log(ANSI_INFO "INFO" ANSI_RESET "  ", domain, __VA_ARGS__)

#define log_variable(_var) \
    log_dbg("variable", "%s=" LOG_FMT_32, xSTR(_var), _var)
#define log_variable_8(_var) \
    log_dbg("variable", "%s=" LOG_FMT_8, xSTR(_var), _var)
#define log_variable_16(_var) \
    log_dbg("variable", "%s=" LOG_FMT_16, xSTR(_var), _var)
#define log_variable_64(_var) \
    log_dbg("variable", "%s=" LOG_FMT_64, xSTR(_var), _var)

#define log_array_fmt(_domain, _arr, _len, _fmt) \
    {                                            \
        log_dbg(_domain, xSTR(_arr));            \
        printf("{ ");                            \
        for (size_t i = 0; i < (_len); ++i)      \
            printf("" _fmt ", ", (_arr)[i]);     \
        printf("}\n");                           \
    }

#define log_array(_domain, _arr, _len) \
    log_array_fmt(_domain, _arr, _len, LOG_FMT_32)
#define log_array_8(_domain, _arr, _len) \
    log_array_fmt(_domain, _arr, _len, LOG_FMT_8)
#define log_array_16(_domain, _arr, _len) \
    log_array_fmt(_domain, _arr, _len, LOG_FMT_16)
#define log_array_64(_domain, _arr, _len) \
    log_array_fmt(_domain, _arr, _len, LOG_FMT_64)

#endif /* KERNEL_LOGGER_H */
