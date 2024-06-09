#include <kernel/error.h>

#include <stddef.h>

static const char *const ERROR_DESCRIPTIONS[E_TOTAL_COUNT] = {
    [E_SUCCESS] = "Success",
    [E_NOMEM] = "Out of memory",
    [E_INVAL] = "Invalid argument",
    [E_NOT_IMPLEMENTED] = "Not implemented",
};

const char *err_to_str(error_t err)
{
    const char *const string = ERROR_DESCRIPTIONS[err];
    if (string == NULL)
        return "Unknown error code";
    return string;
}
