#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

int _isatty(int fd)
{
	if (ioctl(fd, TCISATTY, NULL) == 1)
		return 1;

	if (errno != EBADF)
		errno = ENOTTY;

	return 0;
}
