#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

int ioctl(int fd, unsigned long request, ...);

/*
 * TTY devices.
 */
#define TCISATTY 0

#endif /* _SYS_IOCTL_H */
