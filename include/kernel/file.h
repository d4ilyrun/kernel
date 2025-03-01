/**
 * @defgroup kernel_fs_file File
 * @ingroup kernel_fs
 * @brief File structure
 *
 * @{
 */
#ifndef KERNEL_FILE_H
#define KERNEL_FILE_H

#include <kernel/error.h>

struct vnode;

/** Represents an opened file.
 *
 * @note This struct is also used by pseudo-filesystems, such as the
 * sockfs, used to allocated sockets. According to the UNIX philosophy,
 * everything is a file.
 */
struct file {
    size_t pos;                        ///< Current offset into the file
    void *priv;                        ///< Private data used by the driver
    struct vnode *vnode;               ///< The file's vnode in the VFS
    const struct file_operations *ops; ///< @see file_operations
};

/** Operations that can be performed on an opened file.
 *  @struct file_operations
 */
struct file_operations {
    /**
     * Filesystem-specific open function (optional).
     *
     * If present it is called when opening a new file, and can be used
     * to initialize the file's @c priv field.
     */
    error_t (*open)(struct file *);
    /**
     * Called when closing a file (optional).
     *
     * If present, it should release any memory allocated by the driver to
     * keep trace of the file's internal state.
     */
    void (*close)(struct file *);
    /** Write a buffer to the file at the current position. */
    error_t (*write)(struct file *, const char *, size_t);
    /** Read the content at the current position in the file. */
    error_t (*read)(struct file *, char *, size_t);
    /** Compute the file's total size in memory */
    size_t (*size)(struct file *);
};

/** Create a new file structure */
struct file *file_open(struct vnode *, const struct file_operations *);

/** Free a file struct and release its content */
void file_close(struct file *);

static inline size_t file_size(struct file *file)
{
    if (!file->ops->size)
        return 0;

    return file->ops->size(file);
}

static inline error_t file_write(struct file *file, const char *buf, size_t len)
{
    if (!file->ops->write)
        return E_NOT_SUPPORTED;

    return file->ops->write(file, buf, len);
}

static inline error_t file_read(struct file *file, char *buf, size_t len)
{
    if (!file->ops->read)
        return E_NOT_SUPPORTED;

    return file->ops->read(file, buf, len);
}

#endif /* KERNEL_FILE_H */

/** @} */
