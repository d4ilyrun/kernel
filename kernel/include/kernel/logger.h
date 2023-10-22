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

/**
 * \brief Print an error message onto the terminal.
 *
 * All logging functions will prefix the message with the
 * specified domain name. The latter will change color
 * depending on the logging level.
 *
 * \param domain Used as a prefix to the error message.
 * \param msg The actual message
 */
void log_err(const char *domain, const char *msg);

/// Print a warning message onto the terminal. @see log_err
void log_warn(const char *domain, const char *msg);

/// Print a debug message onto the terminal. @see log_err
void log_dbg(const char *domain, const char *msg);

/// Print a message onto the terminal. @see log_err
void log_info(const char *domain, const char *msg);

#endif /* KERNEL_LOGGER_H */
