#ifndef UAPI_SYS_TYPES_H
#define UAPI_SYS_TYPES_H

/*
 * Redefine our own size_t type.
 *
 * This is needed to avoid LSP errors caused by a conflict between
 * our compiler and the one used by clangd.
 */
#ifdef __SIZE_TYPE__
#undef __SIZE_TYPE__
#endif
#define __SIZE_TYPE__ unsigned long int

typedef long int ssize_t;

/* FIXME: use long for ids (requires changing printk prefix) */
typedef int id_t;
typedef unsigned int pid_t;
typedef int uid_t;
typedef int gid_t;

typedef long int dev_t;

typedef long int off_t;
typedef long int mode_t;
typedef long int nlink_t;
typedef unsigned long int ino_t;
typedef long long int blkcnt_t;
typedef long int blksize_t;

typedef unsigned long long int fsblkcnt_t;
typedef unsigned long long int fsfilcnt_t;

typedef long off_t;

typedef long long int time_t;
typedef long long int clock_t;

#endif /* UAPI_SYS_TYPES_H */
