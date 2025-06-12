#include <kernel/error.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/process.h>
#include <kernel/vfs.h>
#include <uapi/fcntl.h>
#include <uapi/unistd.h>

#include <utils/container_of.h>

#include <string.h>

DECLARE_LLIST(vfs_mountpoints);

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

static error_t
vfs_mount_at(vnode_t *mountpoint, const char *fs_type, u32 start, u32 end)
{
    const vfs_fs_t *fs;
    vfs_t *new;

    if (mountpoint->mounted_here || mountpoint->type != VNODE_DIRECTORY)
        return E_INVAL;

    fs = vfs_find_fs(fs_type);
    if (fs == NULL)
        return E_INVAL;

    new = fs->new(start, end);
    if (IS_ERR(new))
        return ERR_FROM_PTR(new);

    new->node = mountpoint;
    if (mountpoint)
        mountpoint->mounted_here = new;

    llist_add_tail(&vfs_mountpoints, &new->this);
    return E_SUCCESS;
}

error_t vfs_mount_root(const char *fs_type, u32 start, u32 end)
{
    // Mounting a new root is not supported for the time being
    if (!llist_is_empty(&vfs_mountpoints))
        return E_INVAL;

    return vfs_mount_at(NULL, fs_type, start, end);
}

error_t
vfs_mount(const char *mount_path, const char *fs_type, u32 start, u32 end)
{
    vnode_t *mountpoint;
    error_t err;

    mountpoint = vfs_find_by_path(mount_path);
    if (IS_ERR(mountpoint))
        return ERR_FROM_PTR(mountpoint);

    err = vfs_mount_at(mountpoint, fs_type, start, end);
    if (err != E_SUCCESS) {
        vfs_vnode_release(mountpoint);
        return err;
    }

    return E_SUCCESS;
}

static error_t vfs_unmount_at(struct vnode *vnode)
{
    llist_remove(&vnode->mounted_here->this);
    vnode->mounted_here->operations->delete(vnode->mounted_here);

    /* A mounted FS keeps a reference to the vnode it is mounted on */
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

vnode_t *vfs_find_by_path(const char *raw_path)
{
    path_t path = NEW_DYNAMIC_PATH(raw_path);

    vfs_t *fs = vfs_root_fs();
    if (fs == NULL)
        return PTR_ERR(E_NOENT);

    vnode_t *current = fs->operations->root(fs);
    DO_FOREACH_SEGMENT(segment, &path, {
        vnode_t *old;

        // If a filesystem is mounted on the current node, continue the search
        // inside the mounted filesystem.
        if (current->mounted_here) {
            old = current;
            fs = current->mounted_here;
            current = fs->operations->root(fs);
            vfs_vnode_release(old);
        }

        old = current;
        current = current->operations->lookup(current, &segment);
        // NOTE: vfs_vnode_acquire is called inside the `lookup` function in
        //       order to return the new vnoden this is why we must release
        //       this vnode. This looks a bit weird, or 'hidden', and should
        //       maybe be refactored.
        vfs_vnode_release(old);

        if (IS_ERR(current))
            break;
    });

    return current;
}

static vnode_t *vfs_find_parent(path_t *path)
{
    vnode_t *parent;
    char *raw_parent = kmalloc(path->len * sizeof(char), KMALLOC_DEFAULT);
    ssize_t len = path_load_parent(raw_parent, path, path->len);

    if (len == -1) {
        parent = PTR_ERR(E_NOENT);
    } else {
        parent = vfs_find_by_path(raw_parent);
    }

    kfree(raw_parent);

    return parent;
}

static vnode_t *
vfs_create_at(struct vnode *parent, const char *name, vnode_type type)
{
    if (parent->operations->create == NULL)
        return PTR_ERR(E_NOT_SUPPORTED);

    return parent->operations->create(parent, name, type);
}

vnode_t *vfs_create(const char *raw_path, vnode_type type)
{
    path_t path = NEW_DYNAMIC_PATH(raw_path);
    vnode_t *parent = vfs_find_parent(&path);
    path_segment_t file;
    char end_char;
    vnode_t *vnode;

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

    vfs_vnode_release(parent);

    return ret;
}

static struct file *vfs_open_at(struct vnode *vnode)
{
    if (!vnode->operations->open)
        return PTR_ERR(E_NOT_SUPPORTED);

    return vnode->operations->open(vnode);
}

struct file *vfs_open(const char *raw_path)
{
    vnode_t *vnode = vfs_find_by_path(raw_path);

    if (IS_ERR(vnode))
        return (void *)vnode;

    return vfs_open_at(vnode);
}

vnode_t *vfs_vnode_acquire(vnode_t *node, bool *new)
{
    if (node == NULL) {
        node = kcalloc(1, sizeof(vnode_t), KMALLOC_KERNEL);
        if (node == NULL)
            return PTR_ERR(E_NOMEM);
        if (new)
            *new = true;
        INIT_SPINLOCK(node->lock);
    } else {
        if (new)
            *new = false;
    }

    node->refcount += 1;

    return node;
}

vnode_t *vfs_vnode_release(vnode_t *node)
{
    if (node->refcount <= 1) {
        if (node->operations->release)
            node->operations->release(node);
        kfree(node);
        return NULL;
    }

    node->refcount -= 1;
    return node;
}

/*
 * https://pubs.opengroup.org/onlinepubs/9699919799/functions/open.html
 */
int sys_open(const char *path, int oflags)
{
    vnode_t *vnode;
    struct file *file;
    int fd;

    // TODO: open(): ENOTDIR
    // TODO: open(): EROFS
    // TODO: open(): EACCESS

    if (oflags & (O_TRUNC | O_NOCTTY | O_CLOEXEC | O_NONBLOCK))
        return -E_INVAL;

    vnode = vfs_find_by_path(path);

    if (oflags & O_CREAT) {
        /*
         * If O_CREAT and O_EXCL are set, fail if the file exists.
         */
        if (oflags & O_EXCL && !IS_ERR(vnode))
            return -E_EXIST;

        /*
         * TODO: Create file if it does not exist.
         */
        if (IS_ERR(vnode) && ERR_FROM_PTR(vnode) == E_NOENT) {
            vnode = vfs_create(path, VNODE_FILE);
        }
    }

    if (IS_ERR(vnode))
        return -ERR_FROM_PTR(vnode);

    /*
     * Cannot write into a directory node.
     */
    if (vnode->type == VNODE_DIRECTORY && O_WRITABLE(oflags))
        return -E_IS_DIRECTORY;

    if (vnode->type != VNODE_DIRECTORY && oflags & O_DIRECTORY)
        return -E_NOT_DIRECTORY;

    file = vfs_open_at(vnode);
    if (IS_ERR(file))
        return -E_IO;

    file->flags = oflags;
    if (oflags & O_APPEND)
        file_seek(file, 0, SEEK_END);

    fd = process_register_file(current->process, file);
    if (fd < 0)
        file_put(file);

    return fd;
}
