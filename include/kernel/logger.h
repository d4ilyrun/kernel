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
 * When using the logger, one must specify a domain used as a prefix for the
 * message. The domain name is specified by defining the LOG_DOMAIN macro. If
 * this macro is not defined, no prefix will be prefixed to the message.
 *
 * For example, when logging an error related to serial devices, the caller
 * could include this header file as follows:
 *
 * @code
 * #define LOG_DOMAIN "serial"
 * #include <kernel/logger.h>
 *
 * // Then call one of the provided log function
 * log_err("error message goes here");
 * @endcode
 *
 * @{
 */

#ifndef KERNEL_LOGGER_H
#define KERNEL_LOGGER_H

#include <kernel/cpu.h> // read_esp

#include <utils/compiler.h>
#include <utils/stringify.h>

#ifndef LOG_DOMAIN
#define LOG_DOMAIN NULL
#endif

#define FMT8 "%#02hhx"
#define FMT16 "%#04hx"
#define FMT32 "%#08x"
#define FMT64 "%#016llx"

#include <kernel/printk.h>

/** @brief The different available logging levels
 *  @note The lower a level numerical representation is the more important it is
 */
enum log_level {
    LOG_LEVEL_ERR,   /** error messages */
    LOG_LEVEL_WARN,  /** warning messages*/
    LOG_LEVEL_INFO,  /** standard messages */
    LOG_LEVEL_DEBUG, /** debug messages */

    /* Used for indexing purposes only */
    LOG_LEVEL_COUNT,
    LOG_LEVEL_ALL = LOG_LEVEL_COUNT,
};

#define ANSI_RESET "\033[0m"

/**
 * @brief Print a log message onto the terminal.
 *
 * All logging functions will prefix the message with the
 * specified domain name. The latter will change color
 * depending on the logging level.
 *
 * @param level The level of the log
 * @param domain Used as a prefix to the error message.
 * @param msg The actual message
 */
FORMAT(printf, 3, 4)
void log(enum log_level, const char *domain, const char *msg, ...);

void log_vlog(enum log_level, const char *domain, const char *msg,
              va_list parameters);

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

/** Print the callstack to the kernel's console */
void stack_trace(void);

/** @brief Change the maximum log level to display */
void log_set_level(enum log_level);

/** @brief Call the panic function with the appropriate parameters */
#define PANIC(...)                   \
    {                                \
        do {                         \
            u32 esp = read_esp();    \
            panic(esp, __VA_ARGS__); \
        } while (0);                 \
    }

/**
 * Print a log message to the kernel's console
 * @{
 */
#define log_err(format, ...) \
    log(LOG_LEVEL_ERR, LOG_DOMAIN, format __VA_OPT__(, ) __VA_ARGS__)
#define log_warn(format, ...) \
    log(LOG_LEVEL_WARN, LOG_DOMAIN, format __VA_OPT__(, ) __VA_ARGS__)
#define log_info(format, ...) \
    log(LOG_LEVEL_INFO, LOG_DOMAIN, format __VA_OPT__(, ) __VA_ARGS__)
#define log_dbg(format, ...) \
    log(LOG_LEVEL_DEBUG, LOG_DOMAIN, format __VA_OPT__(, ) __VA_ARGS__)
/** @} */

/**
 * @brief Print the content of a variable to the kernel's terminal
 *
 * The name of the variable will be prefixed to the message.
 *
 * There exists different version of this macro, depending on the type
 * (numeric or string), and bit size (for numerics) of the variable.
 *
 * @{
 */
#define log_variable(_var) \
    log(LOG_LEVEL_DEBUG, "variable", "%s=" FMT32, stringify(_var), _var)
#define log_variable_8(_var) \
    log(LOG_LEVEL_DEBUG, "variable", "%s=" FMT8, stringify(_var), _var)
#define log_variable_16(_var) \
    log(LOG_LEVEL_DEBUG, "variable", "%s=" FMT16, stringify(_var), _var)
#define log_variable_64(_var) \
    log(LOG_LEVEL_DEBUG, "variable", "%s=" FMT64, stringify(_var), _var)
#define log_variable_str(_var) \
    log(LOG_LEVEL_DEBUG, "variable", "%s=%s", stringify(_var), _var)
/** @} */

/**
 * @brief Print the content of an array
 *
 * @param _arr The array to print
 * @param _len The number of elements inside the array
 * @param _fmt The format to use for the elements (LOG_FMT_*)
 */
#define log_array_fmt(_arr, _len, _fmt)      \
    {                                        \
        log_dbg(stringify(_arr));            \
        printk("{ ");                        \
        for (size_t i = 0; i < (_len); ++i)  \
            printk("" _fmt ", ", (_arr)[i]); \
        printk("}\n");                       \
    }

/**
 * @brief Print the content of an array
 *
 * There exists different version of this wrapper depending on the size of the
 * types inside the array.
 *
 * @ref log_array_fmt
 * @{
 */
#define log_array(_arr, _len) log_array_fmt(_arr, _len, FMT32)
#define log_array_8(_arr, _len) log_array_fmt(_arr, _len, FMT8)
#define log_array_16(_arr, _len) log_array_fmt(_arr, _len, FMT16)
#define log_array_64(_arr, _len) log_array_fmt(_arr, _len, FMT64)
#define log_array_str(_arr, _len) log_array_fmt(_arr, _len, "%s")
/** @} */

#define not_implemented(...) log_warn("not implemented: " __VA_ARGS__)

#endif /* KERNEL_LOGGER_H */

/** @} */
