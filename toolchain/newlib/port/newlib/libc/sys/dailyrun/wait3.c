#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>

pid_t wait3(int *status, int options, struct rusage *rusage)
{
	(void)rusage;

	return waitpid(-1, status, options);
}
