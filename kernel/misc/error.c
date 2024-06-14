#include <kernel/error.h>

#include <stddef.h>

static const char *const ERROR_DESCRIPTIONS[E_TOTAL_COUNT] = {
    [E_SUCCESS] = "Success",
    [E_NOENT] = "No such file or directory",
    [E_NODEV] = "No such device",
    [E_NOMEM] = "Out of memory",
    [E_INVAL] = "Invalid argument",
    [E_NOT_IMPLEMENTED] = "Not implemented",
    [E_NOT_SUPPORTED] = "Operation not supported",
};

const char *err_to_str(error_t err)
{
    const char *const string = ERROR_DESCRIPTIONS[err];
    if (string == NULL)
        return "Unknown error code";
    return string;
}
