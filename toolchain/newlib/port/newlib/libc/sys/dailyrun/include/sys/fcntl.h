#ifndef _SYS_FCNTL_H
#define _SYS_FCNTL_H

#include <sys/types.h>
#include <sys/stat.h>

/*
 * Flag values for open() and fcntl().
 */

#define	O_RDONLY	0x0000		/* +1 == FREAD */
#define	O_WRONLY	0x0001		/* +1 == FWRITE */
#define	O_RDWR		0x0002		/* +1 == FREAD|FWRITE */
#define	O_APPEND	0x0008
#define	O_CREAT		0x0010
#define	O_TRUNC		0x0020
#define	O_EXCL		0x0040
#define	O_NONBLOCK	0x0080
#define	O_NOCTTY	0x0100

/* POSIX-1.2008 specific flags */
#if __POSIX_VISIBLE >= 200809
#define	O_CLOEXEC	0x1000
#define	O_NOFOLLOW	0x2000
#define	O_DIRECTORY	0x4000
#define	O_EXEC		0x8000
#define	O_SEARCH	O_EXEC
#endif

#define O_ACCMODE (O_RDONLY | O_WRONLY | O_RDWR)
#define O_READABLE(_flags) ((_flags & O_ACCMODE) != O_WRONLY)
#define O_WRITABLE(_flags) ((_flags & O_ACCMODE) != O_RDONLY)

/*
 * File descriptor flags.
 */

#define FD_CLOEXEC 1

/*
 * fcntl() requests
 */

#define	F_DUPFD		0
#define	F_GETFD		1
#define	F_SETFD		2
#define	F_GETFL		3
#define	F_SETFL		4

#define S_IRWU (S_IRUSR | S_IWUSR)
#define S_IRWG (S_IRGRP | S_IWGRP)
#define S_IRWO (S_IROTH | S_IWOTH)
#define S_IRWA (S_IRWU | S_IRWG | S_IRWO)

#ifndef KERNEL

extern int open(const char *, int, ...);
#if __ATFILE_VISIBLE
extern int openat(int, const char *, int, ...);
#endif
extern int creat(const char *, mode_t);
extern int fcntl(int, int, ...);
#if __BSD_VISIBLE
extern int flock(int, int);
#endif
#if __GNU_VISIBLE
#include <sys/time.h>
extern int futimesat(int, const char *, const struct timeval[2]);
#endif

/* for newlib. */
extern int _open(const char *, int, ...);
extern int _fcntl(int, int, ...);
extern int _open64(const char *, int, ...);

#endif

#endif /* _SYS_FCNTL_H */
