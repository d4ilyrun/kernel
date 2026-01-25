#include <kernel/error.h>
#include <kernel/init.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/memory/slab.h>
#include <kernel/process.h>
#include <kernel/vfs.h>
#include <uapi/fcntl.h>

#include <utils/container_of.h>

#include <string.h>
#include <unistd.h>

DECLARE_LLIST(vfs_mountpoints);

static struct kmem_cache *kmem_cache_vnode;

// Defined inside our linkerscript
// These symbols are placed around the '.data.vfs.filesystems' section
extern u32 _kernel_filesystems_start;
extern u32 _kernel_filesystems_end;

static const vfs_fs_t *kernel_filesystems_start =
    (const vfs_fs_t *)&_kernel_filesystems_start;

static const vfs_fs_t *kernel_filesystems_end =
    (const vfs_fs_t *)&_kernel_filesystems_end;

static const vfs_fs_t *vfs_find_fs(const char *fs_type)
{
    for (const vfs_fs_t *fs = kernel_filesystems_start;
         fs < kernel_filesystems_end; ++fs) {
        if (!strcmp(fs->name, fs_type))
            return fs;
    }

    return NULL;
}

static vfs_t *vfs_root_fs()
{
    if (llist_is_empty(&vfs_mountpoints))
        return NULL;

    return container_of(llist_first(&vfs_mountpoints), vfs_t, this);
}

static error_t vfs_mount_at(vnode_t *mountpoint, const char *fs_type,
                            struct block_device *blkdev)
{
    const vfs_fs_t *fs;
    vfs_t *new;

    if (mountpoint &&
        (mountpoint->mounted_here || mountpoint->type != VNODE_DIRECTORY))
        return E_INVAL;

    fs = vfs_find_fs(fs_type);
    if (fs == NULL)
        return E_INVAL;

    new = fs->new(blkdev);
    if (IS_ERR(new))
        return ERR_FROM_PTR(new);

    if (mountpoint) {
        new->node = vfs_vnode_acquire(mountpoint, NULL);
        mountpoint->mounted_here = new;
    } else {
        new->node = NULL; /* root filesystem. */
    }

    llist_add_tail(&vfs_mountpoints, &new->this);
    return E_SUCCESS;
}

error_t vfs_mount_root(const char *fs_type, struct block_device *blkdev)
{
    // Mounting a new root is not supported for the time being
    if (!llist_is_empty(&vfs_mountpoints))
        return E_INVAL;

    return vfs_mount_at(NULL, fs_type, blkdev);
}

error_t vfs_mount(const char *mount_path, const char *fs_type,
                  struct block_device *blkdev)
{
    vnode_t *mountpoint;
    error_t err;

    mountpoint = vfs_find_by_path(mount_path);
    if (IS_ERR(mountpoint))
        return ERR_FROM_PTR(mountpoint);

    err = vfs_mount_at(mountpoint, fs_type, blkdev);

    /* Release the reference acuired by vfs_find_by_path(). */
    vfs_vnode_release(mountpoint);

    return err;
}

static error_t vfs_unmount_at(struct vnode *vnode)
{
    llist_remove(&vnode->mounted_here->this);
    vnode->mounted_here->operations->delete(vnode->mounted_here);

    /* A mounted FS keeps a reference to the vnode it is mounted on */
    vfs_vnode_release(vnode->mounted_here->node);
    vfs_vnode_release(vnode);

    return E_SUCCESS;
}

error_t vfs_unmount(const char *path)
{
    vnode_t *vnode = vfs_find_by_path(path);
    if (IS_ERR(vnode))
        return ERR_FROM_PTR(vnode);

    if (vnode->mounted_here == NULL) {
        vfs_vnode_release(vnode);
        return E_INVAL;
    }

    vfs_unmount_at(vnode);

    /* Release the reference acquired by vfs_find_by_path(). */
    vfs_vnode_release(vnode);

    return E_SUCCESS;
}

/*
 *
 */
static inline struct vnode *
vfs_find_child_at(struct vnode **parent, const path_segment_t *segment)
{
    vnode_t *node = *parent;
    vnode_t *child;
    vfs_t *fs;

    spinlock_acquire(&node->lock);

    /*
     * If a filesystem is mounted on the node continue the search inside
     * the mounted filesystem.
     */
    if (node->mounted_here) {
        fs = node->mounted_here;
        spinlock_release(&node->lock);
        node = fs->operations->root(fs);
        if (IS_ERR(node))
            return node;

        /* free the original parent vnode, replace it with the mounted root. */
        spinlock_acquire(&node->lock);
        vfs_vnode_release(*parent);
        *parent = node;
    }

    child = PTR_ERR(E_NOT_DIRECTORY);
    if (node->type != VNODE_DIRECTORY)
        goto out;

    /* Make sure that the user can search for files inside this directory. */
    locked_scope (&current->process->lock) {
        child = PTR_ERR(E_ACCESS);
        if (!vfs_vnode_check_creds(node, &current->process->creds, O_SEARCH))
            goto out;
    }

    child = node->operations->lookup(node, segment);

out:
    spinlock_release(&node->lock);
    return child;
}

/*
 * Find the vnode pointed at by a path.
 *
 * NOTE: This function increases the vnode's reference count. The reference must
 *       be released by the caller using vfs_vnode_release().
 */
vnode_t *vfs_find_by_path(const char *raw_path)
{
    path_t path = NEW_DYNAMIC_PATH(raw_path);
    vnode_t *node;
    vfs_t *fs;

    fs = vfs_root_fs();
    if (fs == NULL)
        return PTR_ERR(E_NOENT);

    node = fs->operations->root(fs);
    DO_FOREACH_SEGMENT(segment, &path, {
        vnode_t *parent = node;
        node = vfs_find_child_at(&parent, &segment);

        // NOTE: vfs_vnode_acquire() is called inside the `lookup` and `root`
        //       operations, this is why we must release the vnode.
        //       This looks a bit weird, or 'hidden', and should
        //       maybe be refactored.
        vfs_vnode_release(parent);

        if (IS_ERR(node))
            break;
    });

    return node;
}

/*
 * NOTE: Caller must call vfs_vnode_release() on the returned node.
 */
static vnode_t *vfs_find_parent(path_t *path)
{
    vnode_t *parent = NULL;
    char *raw_parent;
    ssize_t len;

    raw_parent = kmalloc(path->len * sizeof(char), KMALLOC_KERNEL);
    if (!raw_parent)
        return PTR_ERR(E_NOMEM);

    len = path_load_parent(raw_parent, path, path->len);
    if (len == -1) {
        parent = PTR_ERR(E_NOENT);
        goto out;
    }

    parent = vfs_find_by_path(raw_parent);

out:
    kfree(raw_parent);
    return parent;
}

static vnode_t *
vfs_create_at(struct vnode *parent, const char *name, vnode_type type)
{
    struct vnode *vnode;

    if (parent->operations->create == NULL)
        return PTR_ERR(E_NOT_SUPPORTED);

    vnode = parent->operations->create(parent, name, type);
    if (IS_ERR(vnode))
        return vnode;

    vnode->stat.st_nlink = 1;

    /* TODO: set file access mode (see opengroup's description of O_CREAT). */
    locked_scope (&current->process->lock) {
        vnode->stat.st_gid = current->process->creds.egid;
        vnode->stat.st_uid = current->process->creds.euid;
    }

    return vnode;
}

vnode_t *vfs_create(const char *raw_path, vnode_type type)
{
    path_t path = NEW_DYNAMIC_PATH(raw_path);
    vnode_t *parent;
    path_segment_t file;
    char end_char;
    vnode_t *vnode;

    parent = vfs_find_parent(&path);
    if (IS_ERR(parent))
        return parent;

    path_walk_last(&path, &file);

    /*
     * Extract the last component from the path and normalize it.
     * The path segment's end is not guaranteed to be the nul character, so
     * we must temporarily insert a nul byte (e.g.: vfs_remove("/usr/bin/")).
     */
    end_char = *file.end;
    *((char *)file.end) = '\0';

    vnode = vfs_create_at(parent, file.start, type);

    /* Undo path normalization. */
    *((char *)file.end) = end_char;

    /* Release reference taken by vfs_find_parent(). */
    vfs_vnode_release(parent);

    return vnode;
}

static error_t vfs_remove_at(struct vnode *parent, const char *name)
{
    if (parent->operations->remove == NULL)
        return E_NOT_SUPPORTED;

    return parent->operations->remove(parent, name);
}

error_t vfs_remove(const char *raw_path)
{
    path_t path = NEW_DYNAMIC_PATH(raw_path);
    path_segment_t file;
    char end_char;
    vnode_t *parent;
    error_t ret;

    parent = vfs_find_parent(&path);
    if (IS_ERR(parent))
        return ERR_FROM_PTR(parent);

    /*
     * Extract the last component from the path and normalize it.
     * @see comment in vfs_create_at().
     */
    path_walk_last(&path, &file);
    end_char = *file.end;
    *((char *)file.end) = '\0';

    ret = vfs_remove_at(parent, file.start);

    /* Undo path normalization. */
    *((char *)file.end) = end_char;

    /* Release reference taken by vfs_find_parent(). */
    vfs_vnode_release(parent);

    return ret;
}

/*
 *
 */
static inline error_t file_compute_flags(int oflags, int *flags)
{
    *flags = 0;

    if (oflags & (O_TRUNC | O_SYNC | O_NONBLOCK | O_NOCTTY | O_NOFOLLOW))
        return -E_INVAL;

    if (O_READABLE(oflags))
        *flags |= FD_READ;
    if (O_READABLE(oflags))
        *flags |= FD_WRITE;

    if (oflags & O_APPEND)
        *flags |= FD_APPEND;
    if (oflags & O_CLOEXEC)
        *flags |= FD_NOINHERIT;

    return E_SUCCESS;
}

/*
 * Open a file description refering to a vnode.
 *
 * NOTE: This function does not release the vnode when an error occurs, so the
 *       caller must check the returned value.
 */
static struct file *vfs_open_at(struct vnode *vnode, int oflags)
{
    struct file *file;
    error_t err;
    int flags;

    err = file_compute_flags(oflags, &flags);
    if (err)
        return PTR_ERR(err);

    locked_scope (&vnode->lock) {

        if (vnode->type != VNODE_DIRECTORY) {
            if (oflags & O_DIRECTORY)
                return PTR_ERR(E_NOT_DIRECTORY);
            if (oflags & O_SEARCH && O_SEARCH != O_EXEC)
                return PTR_ERR(E_NOT_DIRECTORY);
        }

        if (!vnode->operations->open)
            return PTR_ERR(E_NOT_SUPPORTED);

        locked_scope (&current->process->lock) {
            if (!vfs_vnode_check_creds(vnode, &current->process->creds, flags))
                return PTR_ERR(E_PERM);
        }

        file = vnode->operations->open(vnode);
        if (IS_ERR(file))
            return file;
    }

    file->flags = flags;
    if (flags & FD_APPEND)
        file_seek(file, 0, SEEK_END);

    return file;
}

struct file *vfs_open(const char *raw_path, int oflags)
{
    vnode_t *vnode;
    struct file *file;

    vnode = vfs_find_by_path(raw_path);
    if (IS_ERR(vnode))
        return (void *)vnode;

    file = vfs_open_at(vnode, oflags);

    /* Release reference taken by vfs_find_by_path(). */
    vfs_vnode_release(vnode);

    return file;
}

/*
 *
 */
static void vfs_vnode_free(struct vnode *vnode)
{
    kmem_cache_free(kmem_cache_vnode, vnode);
}

/*
 *
 */
static struct vnode *vfs_vnode_alloc(vm_flags_t vm_flags)
{
    return kmem_cache_alloc(kmem_cache_vnode, vm_flags);
}

/*
 *
 */
vnode_t *vfs_vnode_acquire(vnode_t *node, bool *new)
{
    if (node == NULL) {
        node = vfs_vnode_alloc(VM_KERNEL_RW);
        if (node == NULL)
            return PTR_ERR(E_NOMEM);
        if (new)
            *new = true;
    } else {
        if (new)
            *new = false;
    }

    node->refcount += 1;

    return node;
}

/*
 *
 */
vnode_t *vfs_vnode_release(vnode_t *node)
{
    if (!node)
        return NULL;

    if (node->refcount <= 1) {
        if (node->operations->release)
            node->operations->release(node);
        vfs_vnode_free(node);
        return NULL;
    }

    node->refcount -= 1;
    return node;
}

/*
 *
 */
bool vfs_vnode_check_creds(const struct vnode *vnode,
                           const struct user_creds *creds, int oflags)
{
    const struct stat *stat;

    /* Should always be locked to protect against timing attacks. */
    if (WARN_ON(!spinlock_is_held(&vnode->lock)))
        return false;

    stat = &vnode->stat;

#define check_creds_type(_stat, _mode_pfx)                                            \
    do {                                                                       \
        if ((_stat->st_mode & _mode_pfx##USR) && _stat->st_uid == creds->euid) \
            break;                                                             \
        if ((_stat->st_mode & _mode_pfx##GRP) && _stat->st_gid == creds->egid) \
            break;                                                             \
        if (_stat->st_mode & _mode_pfx##OTH)                                   \
            break;                                                             \
        return false;                                                          \
    } while (0)

    if (!creds_is_root(creds)) {
        if (O_READABLE(oflags))
            check_creds_type(stat, S_IR);
        if (O_WRITABLE(oflags))
            check_creds_type(stat, S_IW);
    }

    /* Root still requires rights over the vnode when executing. */
    if (oflags & (O_EXEC | O_SEARCH))
        check_creds_type(stat, S_IX);

#undef check_creds_type

    return true;
}

/*
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/open.html
 */
int sys_open(const char *path, int oflags)
{
    vnode_t *vnode;
    struct file *file;
    int fd;

    // TODO: open(): EROFS

    vnode = vfs_find_by_path(path);

    if (oflags & O_CREAT) {
        /* If O_CREAT and O_EXCL are set, fail if the file exists. */
        fd = -E_EXIST;
        if (oflags & O_EXCL && !IS_ERR(vnode))
            goto error_release_node;

        /* Create file if it does not exist. */
        if (IS_ERR(vnode) && ERR_FROM_PTR(vnode) == E_NOENT) {
            vnode = vfs_create(path, VNODE_FILE);
        }
    }

    if (IS_ERR(vnode))
        return -ERR_FROM_PTR(vnode);

    /*
     * Cannot write into a directory node.
     */
    fd = -E_IS_DIRECTORY;
    if (vnode->type == VNODE_DIRECTORY && O_WRITABLE(oflags))
        goto error_release_node;

    file = vfs_open_at(vnode, oflags);
    if (IS_ERR(file)) {
        fd = -ERR_FROM_PTR(file);
        goto error_release_node;
    }

    fd = process_register_file(current->process, file);
    if (fd < 0)
        file_put(file);

error_release_node:
    vfs_vnode_release(vnode);
    return fd;
}

/*
 * https://pubs.opengroup.org/onlinepubs/9799919799/functions/stat.html
 */
int sys_lstat(const char *path, struct stat *buf)
{
    vnode_t *vnode;

    vnode = vfs_find_by_path(path);
    if (IS_ERR(vnode))
        return -ERR_FROM_PTR(vnode);

    *buf = vnode->stat;

    vfs_vnode_release(vnode);

    return E_SUCCESS;
}

/*
 * https://pubs.opengroup.org/onlinepubs/9799919799/functions/stat.html
 *
 * TODO: If the named file is a symbolic link, the stat() function shall
 * continue pathname resolution using the contents of the symbolic link, and
 * shall return information pertaining to the resulting file if the file
 * exists.
 */
int sys_stat(const char *path, struct stat *buf)
{
    return sys_lstat(path, buf);
}

/*
 * Used by the cache to intialize newly allocated vnodes.
 */
static void vfs_vnode_constructor(void *obj)
{
    struct vnode *vnode = obj;

    INIT_SPINLOCK(vnode->lock);
}

/*
 * Initialize the virtual filesystem API.
 */
error_t vfs_init(void)
{
    kmem_cache_vnode = kmem_cache_create("vnode", sizeof(struct vnode), 64,
                                         vfs_vnode_constructor, NULL, 0);
    if (!kmem_cache_vnode)
        return E_NOMEM;

    return E_SUCCESS;
}

DECLARE_INITCALL(INIT_EARLY, vfs_init);
