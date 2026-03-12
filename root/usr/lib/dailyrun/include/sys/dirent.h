#ifndef _SYS_DIRENT_H
#define _SYS_DIRENT_H

#include <sys/types.h>
#include <sys/stat.h>

typedef struct directory_stream DIR;

struct dirent {
    ino_t d_ino;   // File serial number.
    char d_name[]; // Filename string of entry.
};

struct posix_dent {
    reclen_t d_reclen;    // Length of this entry, including trailing
    unsigned char d_type; // File type or unknown-file-type indication.
    union {
        struct dirent dirent;
        struct {
            ino_t d_ino;          // File serial number.
            char d_name[];        // Filename string of this entry.
        };
    };
};

#define DT_BLK		S_IFBLK  // Block special.
#define DT_CHR		S_IFCHR  // Character special.
#define DT_DIR		S_IFDIR  // Directory.
#define DT_FIFO		S_IFIFO  // FIFO special.
#define DT_LNK		S_IFLNK  // Symbolic link.
#define DT_REG		S_IFREG  // Regular.
#define DT_SOCK		S_IFSOCK // Socket.
#define DT_UNKNOWN	(-1)

#endif /* _SYS_DIRENT_H */
