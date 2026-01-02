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
    E_SUCCESS,                   ///< No error
    E_PERM = 1,                  ///< Operation not permitted
    E_NOENT = 2,                 ///< Entry not found
    E_SRCH = 3,                  ///< No such process
    E_IO = 5,                    ///< I/O error
    E_TOO_BIG = 7,               ///< Arg list too long
    E_CHILD = 10,                ///< No children
    E_WOULD_BLOCK = 11,          ///< Resource temporarily unavailable
    E_NOMEM = 12,                ///< Out of memory
    E_ACCESS = 13,               ///< Permisison denied of memory
    E_BUSY = 16,                 ///< Device or resource busy
    E_EXIST = 17,                ///< File exists
    E_NODEV = 19,                ///< No such device
    E_NOT_DIRECTORY = 20,        ///< Is not a directory
    E_IS_DIRECTORY = 21,         ///< Is a directory
    E_INVAL = 22,                ///< Invalid argument
    E_NFILE = 23,                ///< File table overflow
    E_MFILE = 24,                ///< Too many opened files
    E_SEEK_PIPE = 29,            ///< Illegal seek
    E_READ_ONLY_FS = 30,         ///< Read-only file system
    E_NAME_TOO_LONG = 36,        ///< File name too long
    E_NOT_IMPLEMENTED = 38,      ///< Function not implemented
    E_BAD_FD = 81,               ///< File descriptor in bad state
    E_NOT_SOCKET = 88,           ///< Socket operation on non-socket
    E_DEST_ADDR_REQUIRED = 89,   ///< Destination address required
    E_MSG_SIZE = 90,             ///< Message too long
    E_PROTOTYPE = 91,            ///< Protocol wrong type for socket
    E_NO_PROTO_OPT = 92,         ///< Protocol not available
    E_PROTO_NOT_SUPPORTED = 93,  ///< Protocol not supported
    E_SOCK_T_NOT_SUPPORTED = 94, ///< Socket type not supported
    E_NOT_SUPPORTED = 95,        ///< Operation not supported
    E_PF_NOT_SUPPORTED = 96,     ///< Protocol family not supported
    E_AF_NOT_SUPPORTED = 97,     ///< Address family not supported by protocol
    E_ADDR_IN_USE = 98,          ///< Address already in use
    E_ADDR_NOT_AVAILABLE = 99,   ///< Cannot assign requested address
    E_NET_DOWN = 100,            ///< Network is down
    E_NET_UNREACHABLE = 101,     ///< Network is unreachable
    E_NO_BUFFER_SPACE = 105,     ///< Not enough buffer space
    E_IS_CONNECTED = 106,        ///< Transport endpoint is already connected
    E_NOT_CONNECTED = 107,       ///< Transport endpoint is not connected

    E_TOTAL_COUNT, ///< Total number of error codes, only used as a
                   ///< reference
} error_t;

/** Construct a pointer containing info about an error */
static ALWAYS_INLINE void *PTR_ERR(error_t err)
{
    return (void *)((native_t)-err);
}

/** Extract the error value contained inside a pointer  */
static ALWAYS_INLINE error_t ERR_FROM_PTR(const void *ptr)
{
    return -(error_t)ptr;
}

/** Check if an integer can be interpreted as an error */
#define IS_ERR(_x) ((native_t)(_x) > ((u32) - E_TOTAL_COUNT))

/** Retrieve the string description of an error code */
const char *err_to_str(error_t);

/** @} */
