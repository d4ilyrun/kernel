#ifndef UAPI_FCNTL_H
#define UAPI_FCNTL_H

#define O_RDONLY 0x01
#define O_WRONLY 0x02
#define O_RDWR 0x03
#define O_APPEND 0x08
#define O_CREAT 0x0200
#define O_TRUNC 0x0400
#define O_EXCL 0x0800
#define O_NOCTTY 0x1000
#define O_NONBLOCK 0x4000
#define O_NDELAY O_NONBLOCK
#define O_CLOEXEC 0x40000
#define O_DIRECTORY 0x200000

#define O_ACCMODE (O_RDONLY | O_WRONLY | O_RDWR)

#define O_READABLE(_flags) (_flags & O_RDONLY)
#define O_WRITABLE(_flags) (_flags & O_WRONLY)

#endif /* UAPI_FCNTL_H */
