#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

enum ioctl {
    /* framebuffer (see dailyrun/framebuffer.h) */
    IO_FB_GET_PARAMS,
    IO_FB_GET_BUFFER,
};

int ioctl(int fd, unsigned long request, ...);

#endif /* _SYS_IOCTL_H */
