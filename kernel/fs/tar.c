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
#include <kernel/error.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/vfs.h>

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

} hdr_t ALIGNED(TAR_HEADER_SIZE);

/** @struct tar_filesystem
 *  @brief Filesystem dependant data for a TAR archive
 *
 * This is the data that should go inside the `pdata` field of the containing
 * \ref vfs_fs struct.
 */
typedef struct tar_filesystem {
    tree_t root; ///< Root of this TAR archive's file tree.
                 ///  This tree contains nodes of type \ref tar_node
} tar_t;

/** @struct tar_node
 *  @brief Represents a file (or dir, etc ...) inside a TAR archive's file tree
 */
typedef struct tar_node {
    tree_node_t this; ///< This file's intrusive node used by the tree
    const char *name; ///< The name of this file
    hdr_t *header;    ///< The TAR header corresponding to this file
    vnode_t *vnode;   ///< The vnode corresponding to this file (if referenced)
} tar_node_t;

static vnode_t *tar_get_vnode(tar_node_t *node, vfs_t *fs);

/*  Read a number contained inside a TAR header's field.
 *  Numbers inside a TAR header are written in octal using ascii characters
 *
 *  @param field The ascii representation
 *  @param len The number of bytes inside the field
 */
static size_t tar_read_number(const char *field, size_t len)
{
    size_t count = 0;
    for (size_t i = 0; i < len; ++i) {
        count *= 8;
        count += field[i] - '0';
    }

    return count;
}

static tar_node_t *tar_create_node(const path_segment_t *segment, hdr_t *header)
{
    tar_node_t *new = kmalloc(sizeof(tar_node_t), KMALLOC_KERNEL);
    if (new == NULL) {
        log_err("tarfs", "Failed to allocate memory for a new node (%s)",
                segment->start);
        return PTR_ERR(E_NOMEM);
    }

    size_t name_len = path_segment_length(segment);
    char *name = kmalloc(name_len + 1, KMALLOC_KERNEL);
    if (name == NULL) {
        kfree(new);
        log_err("tarfs", "Failed to allocate memory for a new node (%s)",
                segment->start);
        return PTR_ERR(E_NOMEM);
    }

    strncpy(name, segment->start, name_len);
    name[name_len] = '\0';
    new->name = name;

    // The parent directory are not necessarily added to the tar archive
    // e.g. tar cvf initramfs.tar initramfs/bin/init initramfs/init
    //   => initramfs is not present in the archive
    new->header = path_segment_is_last(segment) ? header : NULL;

    return new;
}

static void tar_free_node(tree_node_t *node)
{
    tar_node_t *tar_node = container_of(node, tar_node_t, this);
    if (tar_node->vnode)
        kfree(tar_node->vnode);
    kfree(node);
}

/* Check whether a tar node's name corresponds to a path segment */
static int tar_node_is(const void *this, const void *segment)
{
    const path_segment_t *name = segment;
    tar_node_t *node = container_of(this, tar_node_t, this);

    return strncmp(node->name, name->start, path_segment_length(name));
}

/* Initialize the file tree of the TAR archive contained inside the device */
static tree_t tar_init_tree(u32 start, u32 end)
{
    UNUSED(end);

    tar_node_t *tar_root = kcalloc(1, sizeof(tar_node_t), KMALLOC_KERNEL);
    if (IS_ERR(tar_root))
        return (tree_t)tar_root;

    tar_root->header = kcalloc(1, sizeof(hdr_t), KMALLOC_KERNEL);
    if (tar_root->header == NULL) {
        kfree(tar_root);
        return PTR_ERR(E_NOMEM);
    }

    tar_root->header->type = TAR_TYPE_DIRECTORY;

    // TODO: Read header from device
    tree_t root = &tar_root->this;
    for (hdr_t *header = (void *)start; header->filename[0] != '\0';) {

        path_t path = NEW_DYNAMIC_PATH(header->filename);
        tree_node_t *current = root;

        if (path.len >= TAR_FILENAME_SIZE)
            continue;

        DO_FOREACH_SEGMENT(segment, &path, {
            tree_node_t *node = tree_find_child(current, tar_node_is, &segment);
            if (!node) {
                tar_node_t *new = tar_create_node(&segment, header);
                if (IS_ERR(new)) {
                    tree_free(&tar_root->this, tar_free_node);
                    return (tree_t) new;
                }
                tree_add_child(current, &new->this);
                node = &new->this;
            }
            current = node;
        });

        size_t size = tar_read_number(header->size, 11);
        header = ((void *)header) + TAR_HEADER_SIZE + size;
        header = (void *)align_up((uint32_t)header, TAR_HEADER_SIZE);
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
    tree_free(tar->root, tar_free_node);
}

static vnode_t *tar_get_vnode(tar_node_t *node, vfs_t *fs)
{
    bool new;

    node->vnode = vfs_vnode_acquire(node->vnode, &new);

    if (new) {
        node->vnode->fs = fs;
        node->vnode->operations = &tar_vnode_ops;
        node->vnode->pdata = node;

        switch (node->header->type) {
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
    }

    return node->vnode;
}

static error_t
tar_vnode_create(vnode_t *vnode, const char *name, vnode_type type)
{
    UNUSED(vnode);
    UNUSED(name);
    UNUSED(type);

    return E_NOT_SUPPORTED;
}

static error_t tar_vnode_remove(vnode_t *vnode, const char *name)
{
    UNUSED(vnode);
    UNUSED(name);

    return E_NOT_SUPPORTED;
}

static vnode_ops_t tar_vnode_ops = {
    .lookup = tar_vnode_lookup,
    .release = tar_vnode_release,
    .create = tar_vnode_create,
    .remove = tar_vnode_remove,
};

static vfs_ops_t tar_vfs_ops = {
    .root = tar_vfs_root,
    .delete = tar_vfs_delete,
};

vfs_t *tar_new(u32 start, u32 end)
{
    log_info("tarfs", "mounting from [" LOG_FMT_32 ":" LOG_FMT_32 "]", start,
             end);

    vfs_t *vfs = kcalloc(1, sizeof(vfs_t), KMALLOC_KERNEL);
    if (vfs == NULL)
        return PTR_ERR(E_NOMEM);

    vfs->operations = &tar_vfs_ops;

    tree_t tree = tar_init_tree(start, end);
    if (IS_ERR(tree)) {
        kfree(vfs);
        return (vfs_t *)tree;
    }

    tar_t *tar_fs = kmalloc(sizeof(*tar_fs), KMALLOC_KERNEL);
    if (tar_fs == NULL) {
        kfree(vfs);
        kfree(tree);
        return PTR_ERR(E_NOMEM);
    }

    tar_fs->root = tree;
    vfs->pdata = tar_fs;

    return vfs;
}

DECLARE_FILESYSTEM(tarfs, tar_new);

/** @} */
