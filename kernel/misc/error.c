#include <kernel/error.h>
#include <kernel/logger.h>

#include <stddef.h>

static const char *const ERROR_DESCRIPTIONS[E_TOTAL_COUNT] = {
    [E_SUCCESS] = "Success",
    [E_PERM] = "Operation not permitted",
    [E_NOENT] = "Entry not found",
    [E_IO] = "I/O error",
    [E_WOULD_BLOCK] = "Resource temporarily unavailable",
    [E_BUSY] = "Device or resource already in use",
    [E_EXIST] = "File exists",
    [E_NODEV] = "No such device",
    [E_NOMEM] = "Out of memory",
    [E_NOT_DIRECTORY] = "Is not a directory",
    [E_IS_DIRECTORY] = "Is a directory",
    [E_INVAL] = "Invalid argument",
    [E_NFILE] = "File table overflow",
    [E_MFILE] = "Too many opened files",
    [E_SEEK_PIPE] = "Illegal seek",
    [E_READ_ONLY_FS] = "Read-only file system",
    [E_NAME_TOO_LONG] = "File name too long",
    [E_NOT_IMPLEMENTED] = "Not implemented",
    [E_BAD_FD] = "File descriptor in bad state",
    [E_NOT_SOCKET] = "Socket operation on non-socket",
    [E_DEST_ADDR_REQUIRED] = "Destination address required",
    [E_MSG_SIZE] = "Message too long",
    [E_PROTOTYPE] = "Protocol wrong type for socket",
    [E_NO_PROTO_OPT] = "Protocol not available",
    [E_PROTO_NOT_SUPPORTED] = "Protocol not supported",
    [E_SOCK_T_NOT_SUPPORTED] = "Socket type not supported",
    [E_NOT_SUPPORTED] = "Operation not supported",
    [E_PF_NOT_SUPPORTED] = "Protocol family not supported",
    [E_AF_NOT_SUPPORTED] = "Address family not supported by protocol",
    [E_ADDR_IN_USE] = "Address already in use",
    [E_ADDR_NOT_AVAILABLE] = "Cannot assign requested address",
    [E_NET_DOWN] = "Network is down",
    [E_NET_UNREACHABLE] = "Network is unreachable",
    [E_NO_BUFFER_SPACE] = "Not enough buffer space",
    [E_IS_CONNECTED] = "Endpoint is already connected",
    [E_NOT_CONNECTED] = "Endpoint is not connected",
};

const char *err_to_str(error_t err)
{
    if (err >= E_TOTAL_COUNT) {
        log_dbg("Invalid error code %d", err);
        return "Invalid error code";
    }

    const char *const string = ERROR_DESCRIPTIONS[err];
    if (string == NULL)
        return "Unknown error code";

    return string;
}
