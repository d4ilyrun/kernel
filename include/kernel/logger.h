/**
 * @defgroup logger Logger
 * @ingroup kernel
 *
 * # Logger
 *
 * Logger library for our kernel
 *
 * ## Usage
 *
 * When using the logger, one must specify a domain
 * used as a prefix to the message.
 *
 * For example, when logging an error related to serial devices,
 * the caller could call the logger the following way:
 *
 *   log_err("serial", "error message goes here");
 *
 * @{
 */

#ifndef KERNEL_LOGGER_H
#define KERNEL_LOGGER_H

#include <kernel/cpu.h> // read_esp

#include <utils/stringify.h>

#define LOG_FMT_8 "%#02hhx"
#define LOG_FMT_16 "%#04hx"
#define LOG_FMT_32 "%#08x"
#define LOG_FMT_64 "%#016llx"

#define ANSI_ERR "\033[31;1;4m"
#define ANSI_WARN "\033[33;1m"
#define ANSI_DBG "\033[36m"
#define ANSI_INFO "\033[39m"
#define ANSI_RESET "\033[0m"

#include <stdio.h>

/**
 * @brief Print a log message onto the terminal.
 *
 * All logging functions will prefix the message with the
 * specified domain name. The latter will change color
 * depending on the logging level.
 *
 * @param type The prefixed log type
 * @param domain Used as a prefix to the error message.
 * @param msg The actual message
 */
void log(const char *type, const char *domain, const char *msg, ...);

/**
 * @brief Completely stop the kernel's execution
 *
 * This function writes a BOLD RED message to the screen, and completely
 * halts the kernel's execution.
 *
 * This should only be called in case of unrecoverable errors, or asserts
 * that should never be false and would prevent the kernel from functioning
 * as expected.
 *
 * @info This function's implementation is arch-specific
 */
void panic(u32 esp, const char *msg, ...) __attribute__((__noreturn__));

/** @brief Call the panic function with the appropriate parameters */
#define PANIC(...)                   \
    {                                \
        do {                         \
            u32 esp = read_esp();    \
            panic(esp, __VA_ARGS__); \
        } while (0);                 \
    }

// TODO: LOG_LEVEL filter

/**
 * @brief Print a log message to the kernel's console
 *
 * When printing a message, you must specify a domain. This domain name will be
 * used as a prefix to te message, and its color ill change depending on the
 * level of log used.
 *
 * @{
 */
#define log_err(domain, ...) \
    log(ANSI_ERR "ERROR" ANSI_RESET " ", domain, __VA_ARGS__)
#define log_warn(domain, ...) \
    log(ANSI_WARN "WARN" ANSI_RESET "  ", domain, __VA_ARGS__)
#define log_dbg(domain, ...) \
    log(ANSI_DBG "DEBUG" ANSI_RESET " ", domain, __VA_ARGS__)
#define log_info(domain, ...) \
    log(ANSI_INFO "INFO" ANSI_RESET "  ", domain, __VA_ARGS__)
/** @} */

/**
 * @brief Print the content of a variable to the kernel's terminal
 *
 * The name of the variable will be prefixed to the message.
 *
 * There exists different version of this macro, dependin on the type (numeric
 * or string), and bit size (for numerics) of the variable.
 *
 * @{
 */
#define log_variable(_var) \
    log_dbg("variable", "%s=" LOG_FMT_32, stringify(_var), _var)
#define log_variable_8(_var) \
    log_dbg("variable", "%s=" LOG_FMT_8, stringify(_var), _var)
#define log_variable_16(_var) \
    log_dbg("variable", "%s=" LOG_FMT_16, stringify(_var), _var)
#define log_variable_64(_var) \
    log_dbg("variable", "%s=" LOG_FMT_64, stringify(_var), _var)
#define log_variable_str(_var) \
    log_dbg("variable", "%s=%s", stringify(_var), _var)
/** @} */

/**
 * @brief Print the content of an array
 *
 * @param _domain log domain's string name
 * @param _arr The array to print
 * @param _len The number of elements inside the array
 * @param _fmt The format to use for the elements (LOG_FMT_*)
 */
#define log_array_fmt(_domain, _arr, _len, _fmt) \
    {                                            \
        log_dbg(_domain, stringify(_arr));       \
        printf("{ ");                            \
        for (size_t i = 0; i < (_len); ++i)      \
            printf("" _fmt ", ", (_arr)[i]);     \
        printf("}\n");                           \
    }

/**
 * @brief Print the content of an array
 *
 * There exists different version of this wrapper depending on the size of the
 * types inside the array.
 *
 * @ref log_array_fmt
 */
#define log_array(_domain, _arr, _len) \
    log_array_fmt(_domain, _arr, _len, LOG_FMT_32)
#define log_array_8(_domain, _arr, _len) \
    log_array_fmt(_domain, _arr, _len, LOG_FMT_8)
#define log_array_16(_domain, _arr, _len) \
    log_array_fmt(_domain, _arr, _len, LOG_FMT_16)
#define log_array_64(_domain, _arr, _len) \
    log_array_fmt(_domain, _arr, _len, LOG_FMT_64)
#define log_array_str(_domain, _arr, _len) \
    log_array_fmt(_domain, _arr, _len, "%s")

#endif /* KERNEL_LOGGER_H */
