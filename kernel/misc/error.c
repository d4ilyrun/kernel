#include <kernel/error.h>
#include <kernel/logger.h>

#include <stddef.h>

static const char *const ERROR_DESCRIPTIONS[E_TOTAL_COUNT] = {
    [E_SUCCESS] = "Success",
    [E_NOENT] = "Entry not found",
    [E_BUSY] = "Device or resource already in use",
    [E_NODEV] = "No such device",
    [E_NOMEM] = "Out of memory",
    [E_INVAL] = "Invalid argument",
    [E_NOT_IMPLEMENTED] = "Not implemented",
    [E_NOT_SUPPORTED] = "Operation not supported",
    [E_NO_BUFFER_SPACE] = "Not enough buffer space",
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
