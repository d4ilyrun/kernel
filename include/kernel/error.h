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

#include <kernel/types.h>

#include <utils/compiler.h>

/**
 * @enum error
 * @brief All the error types used in ths project
 */
typedef enum error {
    E_SUCCESS,              ///< No error
    E_NOENT = 2,            ///< No such file or directory
    E_NOMEM = 12,           ///< Out of memory
    E_NODEV = 19,           ///< No such device
    E_INVAL = 22,           ///< Invalid argument
    E_NOT_IMPLEMENTED = 38, ///< Function not implemented
    E_NOT_SUPPORTED = 95,   ///< Operation not supported
    E_TOTAL_COUNT, ///< Total number of error codes, only used as a reference
} error_t;

/** Construct a pointer containing info about an error */
static ALWAYS_INLINE void *PTR_ERR(error_t err)
{
    return (void *)((native_t)-err);
}

/** Extract the error value contained inside a pointer  */
static ALWAYS_INLINE error_t ERR_FROM_PTR(void *ptr)
{
    return -(error_t)ptr;
}

/** Check if an integer can be interpreted as an error */
#define IS_ERR(_x) ((native_t)(_x) > ((u32)-E_TOTAL_COUNT))

/** Retrieve the string description of an error code */
const char *err_to_str(error_t);

/** @} */
