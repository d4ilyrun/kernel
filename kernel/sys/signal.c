#include <kernel/error.h>

#include <utils/macro.h>

int sys_kill(pid_t pid, int signal)
{
    UNUSED(pid);
    UNUSED(signal);

    return -E_NOT_SUPPORTED;
}
