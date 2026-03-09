#include <sys/signal.h>
#include <sys/types.h>

int killpg(pid_t pid, int signal)
{
    return kill(-pid, signal);
}
