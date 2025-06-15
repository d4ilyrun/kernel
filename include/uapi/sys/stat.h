#ifndef UAPI_SYS_STAT_H
#define UAPI_SYS_STAT_H

#include <uapi/sys/types.h>
#include <uapi/time.h>

struct stat {
    dev_t st_dev;
    ino_t st_ino;
    mode_t st_mode;
    nlink_t st_nlink;
    uid_t st_uid;
    gid_t st_gid;
    dev_t st_rdev;
    off_t st_size;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
    blksize_t st_blksize;
    blkcnt_t st_blocks;
    long st_spare4[2];
};

#define S_BLKSIZE 1024 /* size of a block */

#define S_ISUID 0004000 /* set user id on execution */
#define S_ISGID 0002000 /* set group id on execution */
#define S_ISVTX 0001000 /* save swapped text even after use */

#define S_IFMT 0170000   /* type of file */
#define S_IFDIR 0040000  /* directory */
#define S_IFCHR 0020000  /* character special */
#define S_IFBLK 0060000  /* block special */
#define S_IFREG 0100000  /* regular */
#define S_IFLNK 0120000  /* symbolic link */
#define S_IFSOCK 0140000 /* socket */
#define S_IFIFO 0010000  /* fifo */

#define S_IRWXU (S_IRUSR | S_IWUSR | S_IXUSR)
#define S_IRWU (S_IRUSR | S_IWUSR)
#define S_IRUSR 0000400 /* read permission, owner */
#define S_IWUSR 0000200 /* write permission, owner */
#define S_IXUSR 0000100 /* execute/search permission, owner */
#define S_IRWXG (S_IRGRP | S_IWGRP | S_IXGRP)
#define S_IRWG (S_IRGRP | S_IWGRP)
#define S_IRGRP 0000040 /* read permission, group */
#define S_IWGRP 0000020 /* write permission, grougroup */
#define S_IXGRP 0000010 /* execute/search permission, group */
#define S_IRWXO (S_IROTH | S_IWOTH | S_IXOTH)
#define S_IRWO (S_IROTH | S_IWOTH)
#define S_IROTH 0000004 /* read permission, other */
#define S_IWOTH 0000002 /* write permission, other */
#define S_IXOTH 0000001 /* execute/search permission, other */

#define S_ISBLK(m) (((m) & S_IFMT) == S_IFBLK)
#define S_ISCHR(m) (((m) & S_IFMT) == S_IFCHR)
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

#endif /* UAPI_SYS_STAT_H */
