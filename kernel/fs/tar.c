/**
 * @defgroup vfs_tar TAR
 * @ingroup kernel_fs
 *
 * @brief Tape ARchive format.
 *
 * # Tape ARchive format.
 *
 * It is a common general-purpose archive format, with tools to generate
 * such archives available on every OS.
 *
 * @note As an archive format, the TAR filesystem is read-only by design.
 *
 * @see https://wiki.osdev.org/Tar
 *
 * @{
 */

#define LOG_DOMAIN "tarfs"

#include <kernel/devices/block.h>
#include <kernel/error.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/vfs.h>
#include <uapi/limits.h>

#include <lib/path.h>
#include <libalgo/tree.h>
#include <utils/compiler.h>
#include <utils/container_of.h>
#include <utils/macro.h>
#include <utils/math.h>

#include <stddef.h>
#include <string.h>

static vfs_ops_t tar_vfs_ops;
static vnode_ops_t tar_vnode_ops;
static struct file_operations tar_file_ops;

/** Pathname length inside a TAR format is limited to 99 characters (size
 *  includes the NULL terminating byte)
 */
#define TAR_FILENAME_SIZE 100

/** Each file gets a header that is padded up to 512 bytes (and is aligned on a
 *  512 bytes boundary)
 */
#define TAR_HEADER_SIZE 512

/** @struct tar_header
 *  @brief Header located at the beginning of each file in the archive.
 */
typedef struct tar_header {
    char filename[TAR_FILENAME_SIZE]; ///< Full path of the file
    char mode[8];                     ///< Permissions
    char uid[8];                      ///< Owner's numeric ID
    char gid[8];                      ///< Group's numeric ID
    char size[12];                    ///< Size of the file (excluding header)
    char mtime[12];                   ///< Last modification time
    char chksum[8];                   ///< Checksum

    /** @enum tar_typeflag
     *  @brief Different available types for a file inside a TAR archive
     */
    enum tar_typeflag {
        TAR_TYPE_FILE = '0',
        TAR_TYPE_HARDLINK = '1',
        TAR_TYPE_SYMLINK = '2',
        TAR_TYPE_CHARDEV = '3',
        TAR_TYPE_BLOCKDEV = '4',
        TAR_TYPE_DIRECTORY = '5',
        TAR_TYPE_FIFO = '6',
    } type; ///< The type of this file

} ALIGNED(TAR_HEADER_SIZE) hdr_t;

static_assert(sizeof(struct tar_header) == TAR_HEADER_SIZE);

/** @struct tar_filesystem
 *  @brief Filesystem dependant data for a TAR archive
 *
 * This is the data that should go inside the `pdata` field of the containing
 * \ref vfs_fs struct.
 */
typedef struct tar_filesystem {
    struct vfs *vfs;
    tree_t root; ///< Root of this TAR archive's file tree.
                 ///  This tree contains nodes of type \ref tar_node
} tar_t;

/** @struct tar_node
 *  @brief Represents a file (or dir, etc ...) inside a TAR archive's file tree
 */
typedef struct tar_node {
    struct tar_header header; ///< The TAR header corresponding to this file
    char name[NAME_MAX];      ///< The name of this file
    tree_node_t this;         ///< This file's intrusive node used by the tree
    vnode_t *vnode; ///< The vnode corresponding to this file (if referenced)
    size_t size;    ///< The file's size
    off_t offset;   ///< File offset into the block device (including header)
} tar_node_t;

static vnode_t *tar_get_vnode(tar_node_t *node, vfs_t *fs);

/*  Read a number contained inside a TAR header's field.
 *  Numbers inside a TAR header are written in octal using ascii characters
 *
 *  @param field The ascii representation
 *  @param len The number of bytes inside the field
 */
static size_t tar_read_number_len(const char *field, size_t len)
{
    size_t count = 0;
    for (size_t i = 0; i < len; ++i) {
        if (field[i] == '\0')
            break;
        count *= 8;
        count += field[i] - '0';
    }

    return count;
}

#define tar_read_number(_number) tar_read_number_len(_number, sizeof(_number))

/*
 * Parent directories are not present inside a tar archive.
 *
 * To circumvent this, we use this virtual empty directory TAR header instead
 * to replace the one that would have been here.
 */
static struct tar_header tar_virtual_dir_hdr = {
    .type = TAR_TYPE_DIRECTORY,
};

/*
 * Read the content of the underlying block device at @c offset into @c buffer.
 *
 * This function can only read from one block at a time. It returns E_INVAL
 * if the requested read spans over multiple blocks.
 */
static ssize_t
tar_read(struct tar_filesystem *fs, void *buffer, off_t offset, size_t size)
{
    struct block_device *blkdev = fs->vfs->blkdev;
    blkcnt_t index;
    void *block;

    index = offset / blkdev->block_size;
    offset %= blkdev->block_size;

    if (offset + size > (size_t)blkdev->block_size)
        return -E_INVAL;

    block = block_read(blkdev, index);
    if (IS_ERR(block))
        return -ERR_FROM_PTR(block);

    memcpy(buffer, block + offset, size);
    block_free(blkdev, block);

    return size;
}

static void
tar_node_set_name(struct tar_node *node, const char *name, size_t size)
{
    strlcpy(node->name, name, MIN(size + 1, sizeof(node->name)));
}

static struct tar_node *tar_node_alloc(struct tar_filesystem *fs)
{
    struct block_device *blkdev = fs->vfs->blkdev;
    struct tar_node *new;

    new = kcalloc(1, sizeof(*new), KMALLOC_KERNEL);
    if (new == NULL) {
        log_err("%s: failed to allocate memory for a new node",
                blkdev->dev.name);
        return PTR_ERR(E_NOMEM);
    }

    INIT_TREE_NODE(new->this);

    return new;
}

static void tar_node_free(tree_node_t *node)
{
    tar_node_t *tar_node = container_of(node, tar_node_t, this);
    /* Do not free vnode, let it be free'd by vnode_release(). */
    if (tar_node->vnode)
        tar_node->vnode->pdata = NULL;
    kfree(node);
}

/*
 * Allocate and initialize a node from a header present on disk.
 */
static struct tar_node *
tar_node_from_header(struct tar_filesystem *fs, off_t hdr_offset)
{
    struct tar_node *new;

    new = tar_node_alloc(fs);
    if (IS_ERR(new))
        return new;

    tar_read(fs, &new->header, hdr_offset, TAR_HEADER_SIZE);
    if (new->header.filename[0] == '\0') {
        tar_node_free(&new->this);
        return NULL;
    }

    new->offset = hdr_offset;
    new->size = tar_read_number(new->header.size);

    return new;
}

/*
 * Allocate and initialize a virtual directory node.
 * See the loop in @ref tar_init_tree() for more details.
 */
static struct tar_node *
tar_virtual_dir_node(struct tar_filesystem *fs, path_segment_t *segment)
{
    struct tar_node *new;

    new = tar_node_alloc(fs);
    if (IS_ERR(new))
        return new;

    memcpy(&new->header, &tar_virtual_dir_hdr, TAR_HEADER_SIZE);

    new->size = 0;
    new->offset = 0xdeadbeef;
    tar_node_set_name(new, segment->start, path_segment_length(segment));

    return new;
}

/* Check whether a tar node's name corresponds to a path segment */
static int tar_node_is(const void *this, const void *segment)
{
    const path_segment_t *name = segment;
    tar_node_t *node = container_of(this, tar_node_t, this);

    return strncmp(node->name, name->start, path_segment_length(name));
}

/* Initialize the file tree of the TAR archive contained inside the device */
static tree_t tar_init_tree(struct tar_filesystem *fs)
{
    struct tar_node *tar_root;
    struct tar_node *tar_node;
    tree_t root;
    off_t offset;

    tar_root = tar_node_alloc(fs);
    if (IS_ERR(tar_root))
        return (tree_t)tar_root;

    root = &tar_root->this;
    tar_root->header.type = TAR_TYPE_DIRECTORY;

    offset = 0;
    while ((tar_node = tar_node_from_header(fs, offset)) != NULL) {
        struct tar_header *header = &tar_node->header;
        path_t path = NEW_DYNAMIC_PATH(header->filename);
        tree_node_t *current = root;

        if (path.len >= TAR_FILENAME_SIZE)
            continue;

        /*
         * A tar archive does not necessarily need to include a directory
         * if it already contains the full path to a file inside it. For
         * example, '/usr/bin' is not guaranteed to be present if
         * '/usr/bin/init' is.
         *
         * This loop adds the required directory entries to construct the path
         * specified in the node's header (if not already present).
         */
        DO_FOREACH_SEGMENT(segment, &path, {
            tar_node_t *new = NULL;
            if (!path_segment_is_last(&segment)) {
                tree_node_t *node = tree_find_child(current, tar_node_is,
                                                    &segment);
                if (node) {
                    current = node;
                    continue;
                }
                new = tar_virtual_dir_node(fs, &segment);
                if (IS_ERR(new)) {
                    tree_free(&tar_root->this, tar_node_free);
                    return (tree_t) new;
                }
            } else {
                /* tar_node_from_header() does not set the tar_node's name. */
                tar_node_set_name(tar_node, segment.start,
                                  path_segment_length(&segment));
                new = tar_node;
            }
            tree_add_child(current, &new->this);
            current = &new->this;
        });

        offset += align_up(tar_node->size + TAR_HEADER_SIZE, TAR_HEADER_SIZE);
    }

    return root;
}

static vnode_t *tar_vnode_lookup(vnode_t *node, const path_segment_t *child)
{
    if (node->type == VNODE_FILE)
        return PTR_ERR(E_NOT_SUPPORTED);
    if (node->type != VNODE_DIRECTORY)
        return PTR_ERR(E_NOT_IMPLEMENTED);

    tar_node_t *tar_node = node->pdata;
    tree_node_t *tar_child = tree_find_child(&tar_node->this, tar_node_is,
                                             child);

    if (tar_child == NULL)
        return PTR_ERR(E_NOENT);

    return tar_get_vnode(container_of(tar_child, tar_node_t, this), node->fs);
}

static void tar_vnode_release(vnode_t *vnode)
{
    tar_node_t *node = vnode->pdata;

    /* Can be null if unmounted when still holding a reference */
    if (node != NULL)
        node->vnode = NULL;
}

static vnode_t *tar_vfs_root(vfs_t *fs)
{
    tar_t *tar = fs->pdata;
    tar_node_t *root = container_of(tar->root, tar_node_t, this);

    return tar_get_vnode(root, fs);
}

static void tar_vfs_delete(vfs_t *vfs)
{
    tar_t *tar = vfs->pdata;
    tree_free(tar->root, tar_node_free);
}

static vnode_t *tar_get_vnode(tar_node_t *node, vfs_t *fs)
{
    struct tar_header *header = &node->header;
    struct stat *stat;
    bool new;

    node->vnode = vfs_vnode_acquire(node->vnode, &new);
    node->size = tar_read_number(header->size);

    if (!new)
        return node->vnode;

    node->vnode->fs = fs;
    node->vnode->operations = &tar_vnode_ops;
    node->vnode->pdata = node;

    /*
     * Initialize vnode statistics.
     *
     * The TAR file system does not use an inode number nor block IO, so
     * these fields are kept empty. Since this is a read-only filesystem,
     * we'll also omit the st_ctime field (the file's status is fixed).
     *
     * Because the TAR header does not include any access time information
     * we set it to the time of the last modification time until the file is
     * accessed for real.
     *
     * TODO: File device information.
     */
    stat = &node->vnode->stat;
    stat->st_size = node->size;
    stat->st_mode = tar_read_number(header->mode);
    stat->st_uid = tar_read_number(header->uid);
    stat->st_gid = tar_read_number(header->gid);
    stat->st_mtim.tv_nsec = tar_read_number(header->mtime);
    stat->st_atim = stat->st_mtim;
    stat->st_nlink = 1;

    switch (header->type) {
    case TAR_TYPE_FILE:
        node->vnode->type = VNODE_FILE;
        break;
    case TAR_TYPE_HARDLINK:
    case TAR_TYPE_SYMLINK:
        node->vnode->type = VNODE_SYMLINK;
        break;
    case TAR_TYPE_CHARDEV:
        node->vnode->type = VNODE_CHARDEVICE;
        break;
    case TAR_TYPE_BLOCKDEV:
        node->vnode->type = VNODE_BLOCKDEVICE;
        break;
    case TAR_TYPE_DIRECTORY:
        node->vnode->type = VNODE_DIRECTORY;
        break;
    case TAR_TYPE_FIFO:
        node->vnode->type = VNODE_FIFO;
        break;
    }

    return node->vnode;
}

static vnode_t *
tar_vnode_create(vnode_t *vnode, const char *name, vnode_type type)
{
    UNUSED(vnode);
    UNUSED(name);
    UNUSED(type);

    return PTR_ERR(E_READ_ONLY_FS);
}

static error_t tar_vnode_remove(vnode_t *vnode, const char *name)
{
    UNUSED(vnode);
    UNUSED(name);

    return E_READ_ONLY_FS;
}

static struct file *tar_vnode_open(vnode_t *vnode)
{
    return file_open(vnode, &tar_file_ops);
}

static vnode_ops_t tar_vnode_ops = {
    .lookup = tar_vnode_lookup,
    .release = tar_vnode_release,
    .create = tar_vnode_create,
    .open = tar_vnode_open,
    .remove = tar_vnode_remove,
};

static vfs_ops_t tar_vfs_ops = {
    .root = tar_vfs_root,
    .delete = tar_vfs_delete,
};

static size_t tar_file_size(struct file *file)
{
    vnode_t *vnode = file->vnode;
    tar_node_t *tar_node = vnode->pdata;

    return tar_node->size;
}

static ssize_t tar_file_read(struct file *file, char *buffer, size_t len)
{
    vnode_t *vnode = file->vnode;
    struct tar_node *tar_node = vnode->pdata;
    struct block_device *blkdev = vnode->fs->blkdev;
    off_t block_offset;
    off_t offset;
    ssize_t read;

    offset = tar_node->offset + TAR_HEADER_SIZE + file->pos;
    block_offset = offset % blkdev->block_size;

    if (file->pos + len < (size_t)file->pos || file->pos + len > tar_node->size)
        return -E_INVAL;

    /* Reduce read length to fit inside a single block. */
    if (block_offset + len > (size_t)blkdev->block_size)
        len = blkdev->block_size - block_offset;

    read = tar_read(vnode->fs->pdata, buffer, offset, len);
    if (read < 0)
        return read;

    file->pos += read;

    return read;
}

static struct file_operations tar_file_ops = {
    .size = tar_file_size,
    .read = tar_file_read,
    .seek = default_file_seek,
};

vfs_t *tar_new(struct block_device *blkdev)
{
    struct tar_filesystem *tar_fs = NULL;
    struct vfs *vfs = NULL;
    tree_t tree;
    error_t err;

    vfs = kcalloc(1, sizeof(vfs_t), KMALLOC_KERNEL);
    if (vfs == NULL)
        return PTR_ERR(E_NOMEM);

    tar_fs = kmalloc(sizeof(*tar_fs), KMALLOC_KERNEL);
    if (tar_fs == NULL) {
        err = E_NOMEM;
        goto tar_new_fail;
    }

    tar_fs->vfs = vfs;

    vfs->operations = &tar_vfs_ops;
    vfs->blkdev = blkdev;
    vfs->pdata = tar_fs;

    tree = tar_init_tree(tar_fs);
    if (IS_ERR(tree)) {
        kfree(vfs);
        return (vfs_t *)tree;
    }

    tar_fs->root = tree;

    return vfs;

tar_new_fail:
    kfree(vfs);
    kfree(tar_fs);
    return PTR_ERR(err);
}

DECLARE_FILESYSTEM(tarfs, tar_new);

/** @} */
