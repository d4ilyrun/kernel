#include <sys/errno.h>
#include <sys/types.h>
#include <unistd.h>

int getgroups(int size, gid_t list[])
{
	if (size == 0)
		return 1;

	if (size < 1) {
		errno = EINVAL;
		return -1;
	}

	list[0] = getgid();

	return 1;
}
