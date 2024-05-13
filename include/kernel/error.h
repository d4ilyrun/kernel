#pragma once

/**
 * @file kernel/error.h
 *
 * @defgroup kernel_error Errors
 * @ingroup kernel
 *
 * # Errors
 *
 * These are the error types, functions and handlers used all throughout the
 * project.
 *
 * If a new interface must return an error value it should, as much as possible,
 * use or update what's defined inside this file.
 *
 * @{
 */

/**
 * @enum error
 * @brief All the error types used in ths project
 */
typedef enum error {
    E_NONE,       ///< No error
    E_NOMEM = 12, ///< Out of memory
    E_INVAL = 22, ///< Invalid argument
} error_t;

/** Check if an integer can be interpreted as an error */
#define IS_ERR(_x) ((_x) < 0)
