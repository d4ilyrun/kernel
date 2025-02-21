#pragma once

/**
 * @file kernel/vfs.h
 *
 * @defgroup kernel_fs Filesystem
 * @ingroup kernel
 *
 * @defgroup kernel_vfs VFS
 * @ingroup kernel_fs
 * @brief Virtual File System
 *
 * # Virtual Filesystem
 *
 * The virtual is an abstraction over filesystems used to separate the high
 * level interface from the low level fs-dependent implementation.
 * It also allows for computing a path which spans over multiple different
 * imbricked filesystems, effectively viewing it as a single big filesystem from
 * the user's POV.
 *
 * ## Design
 *
 * The VFS splits each mounted filesystem into 2 parts:
 * * The virtual filesystem: The filesystem as a whole
 * * Virtual nodes: The individual components (files, dirs, ...) that make up
 *                  this filesystem
 *
 * The VFS driver keeps track of all the present virtual filesystem, and
 * addresses them in order to find a requested path.
 * When "opening" a file, we instead ask the corresponding VFS to create the
 * VNODE which corresponds to this file, and interact with it instead. All the
 * filesystem dependant logic is hidden from the VFS driver.
 *
 * @see http://www.cs.fsu.edu/~awang/courses/cop5611_s2024/vnode.pdf
 *
 * @{
 */

#include <kernel/error.h>
#include <kernel/file.h>
#include <kernel/types.h>

#include <lib/path.h>
#include <libalgo/linked_list.h>
#include <utils/compiler.h>
#include <utils/stringify.h>

typedef struct vfs vfs_t;
typedef struct vnode vnode_t;

typedef enum vnode_type vnode_type;

/** @defgroup fs_vfs_vfs Virtual filesystem
 *  @{
 */

/** @struct vfs_operations
 *  @brief Vector Table for operations on a filesystems
 */
typedef struct vfs_operations {
    /** Retreive the root of the filesystem */
    vnode_t *(*root)(vfs_t *);
    /** Free internal structures and vnodes.
     *  This function is called by the driver before unmounting the filesystem.
     */
    void (*delete)(vfs_t *);
} vfs_ops_t;

/**
 * @struct vfs
 * @brief represents a single virtual filesystem
 */
typedef struct vfs {
    node_t this;
    vfs_ops_t *operations; ///< @ref vfs_operations
    vnode_t *node;         ///< vnode on which this FS is mounted
    void *pdata;           ///< Private FS dependent data
} vfs_t;

/** Mount a filesystem of the given type at a given path.
 *
 * @param path Where the fs should be mounted (must be an existing
 *                   directory)
 * @param fs_type The name of the filesystem to mount
 *
 * TODO: TEMPORARY: Should be replaced with a device
 * @param start Start of the filesystem's address range
 * @param end End of the filesystem's address range
 *
 * @return
 *          * E_INVAL - the filesystem does not exist
 *          * E_INVAL - the path does not lead to a directory
 *          * E_INVAL - another filesystem is mounted at the requested path
 *          * E_NOENT - the path is invalid
 */
error_t vfs_mount(const char *path, const char *fs_type, u32 start, u32 end);

/** Mount a filesystem at the root of the VFS.
 *
 * @param fs_type The type of the filesystem
 *
 * TODO: TEMPORARY: Should be replaced with a device
 * @param start Start of the filesystem's address range
 * @param end End of the filesystem's address range
 *
 * @return
 *          * E_INVAL - there already is a root for the VFS
 *          * E_INVAL - the filesystem does not exist
 */
error_t vfs_mount_root(const char *fs_type, u32 start, u32 end);

/** Unmount the filesystem present at the given path.
 *
 * @return
 *      * E_INVAL - No file system mounted on the given path
 */
error_t vfs_unmount(const char *path);

/** Retreive the vnode corresponding to a path
 *
 *  @warning The vnode returned by this function **must** be released using
 *           \ref vfs_vnode_release once done with it
 *
 *  @return The vnode, or a pointer containing the error number:
 *      * E_NOENT - Path does not exist
 */
vnode_t *vfs_find_by_path(const char *path);

/** Create a new file at the given path
 *
 *  @param path The path of the new child
 *  @param type The type of the new file
 *
 *  @return filesystem specific errors, or:
 *      * E_NOENT - Path is invalid
 *      * E_EXIST - File already exists
 */
error_t vfs_create_at(const char *path, vnode_type type);

/** Remove the file located at the given path
 *
 *  @return E_NOENT if the file does not exist
 */
error_t vfs_remove_at(const char *path);

/** Open the file located at the given path
 *  @return The file's struct, or a pointed encoded error
 */
struct file *vfs_open_at(const char *path);

/** @} */

/** @defgroup fs_vfs_vnode Virtual nodes
 *  @{
 */

/** @enum vnode_type
 *  @brief The different existing types of vnodes
 */
typedef enum vnode_type {
    VNODE_FIFO = 0x1000,
    VNODE_CHARDEVICE = 0x2000,
    VNODE_DIRECTORY = 0x4000, ///< Regular directory
    VNODE_BLOCKDEVICE = 0x6000,
    VNODE_FILE = 0x8000,    ///< Regular file
    VNODE_SYMLINK = 0xA000, ///< Symbolic link
    VNODE_SOCKET = 0xC000,
} vnode_type;

/** @struct vnode_operations
 *  @brief Vector table for operations performed on a single virtual node
 */
typedef struct vnode_operations {

    /** Find a child node (by name) inside a directory node.
     *  @warning The node returned by this function MUST be released afterwards
     *           using \ref vfs_vnode_release (subject to change in the future).
     */
    vnode_t *(*lookup)(vnode_t *, const path_segment_t *);

    /** Add a new child to the vnode. */
    error_t (*create)(vnode_t *node, const char *name, vnode_type);

    /** Remove a child from a directory */
    error_t (*remove)(vnode_t *node, const char *child);

    /** Create a new opened file corresponding to this vnode  */
    struct file *(*open)(vnode_t *vnode);

    /** Called by the VFS driver before deleting a vnode (optional).
     *  This is responsible for freeing/updating any necessary internal
     *  structures before deleting a vnode.
     */
    void (*release)(vnode_t *node);

} vnode_ops_t;

/** @struct vnode
 *  @brief represents a single virtual node
 */
struct vnode {
    vfs_t *fs;               ///< Filesystem to which this node belong
    vnode_type type;         ///< Type of the node
    u16 refcount;            ///< Number of references hold to that node
    vnode_ops_t *operations; ///< @ref vnode_operations
    void *pdata;             ///< Private node data
    vfs_t *mounted_here;     ///< Potential filesystem mounted over this node
};

/** Increment the refcount of a vnode
 *
 * Similarily to any other synchronization mechanism, you must call
 * \ref vfs_vnode_release once you are done using this node
 *
 * @param vnode The referenced vnode
 * @param[out] new Set to true if a new vnode was created (optional)
 *
 * @return The original vnode, or a newly allocated one if NULL
 */
vnode_t *vfs_vnode_acquire(vnode_t *, bool *);

/** Decrease the refcount of a vnode
 *
 * @warning The vnode is free'd if the new refcount is 0
 *
 * @return The original vnode, NULL if the new refcount is 0
 */
vnode_t *vfs_vnode_release(vnode_t *);

/** @} */

/** @struct vfs_fs
 *  @brief Represents a file system format
 *
 *  This structure is used by the VFS driver to mount the filesystem.
 */
typedef ALIGNED(ARCH_WORD_SIZE) struct vfs_fs {
    const char *const name;  ///< Name of the filesystem
    vfs_t *(*new)(u32, u32); ///< Create a new instance of this filesystem
                             ///< using the given device
} vfs_fs_t;

/** Declare a new available filesystem.
 *
 * A file system **needs** to be declared using this macro for the VFS driver
 * to be able to mount it using \c vfs_mount.
 *
 * @param fs_name The name of the filesystem
 * @param fw_new The function used to create a new instance of this filesystem
 */
#define DECLARE_FILESYSTEM(fs_name, fs_new)      \
    SECTION(".data.vfs.filesystems")             \
    MAYBE_UNUSED                                 \
    static vfs_fs_t fs_name##_fs_declaration = { \
        .name = stringify(fs_name),              \
        .new = fs_new,                           \
    }

/** @} */
