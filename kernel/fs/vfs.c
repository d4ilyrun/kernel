#include <kernel/error.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/vfs.h>

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
vfs_mount_at(vnode_t *vnode, const char *fs_type, u32 start, u32 end)
{
    const vfs_fs_t *fs = vfs_find_fs(fs_type);
    if (fs == NULL)
        return E_INVAL;

    vfs_t *new = fs->new (start, end);
    if (IS_ERR(new))
        return ERR_FROM_PTR(new);

    new->node = vnode;
    if (vnode)
        vnode->mounted_here = new;

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
    vnode_t *mountpoint = vfs_find_by_path(mount_path);
    if (IS_ERR(mountpoint))
        return ERR_FROM_PTR(mountpoint);

    if (mountpoint->mounted_here != NULL ||
        mountpoint->type != VNODE_DIRECTORY) {
        vfs_vnode_release(mountpoint);
        return E_INVAL;
    }

    error_t err = vfs_mount_at(mountpoint, fs_type, start, end);
    if (err != E_SUCCESS) {
        vfs_vnode_release(mountpoint);
        return err;
    }

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

    llist_remove(&vnode->mounted_here->this);
    vnode->mounted_here->operations->delete (vnode->mounted_here);
    // The mounted FS kept a reference to the vnode it was mounted on
    vfs_vnode_release(vnode);

    // Release the reference acquired by vfs_find_by_path
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

error_t vfs_create_at(const char *raw_path, vnode_type type)
{
    error_t ret;
    path_t path = NEW_DYNAMIC_PATH(raw_path);
    vnode_t *parent = vfs_find_parent(&path);

    if (IS_ERR(parent))
        return ERR_FROM_PTR(parent);

    if (parent->operations->create == NULL) {
        ret = E_NOT_IMPLEMENTED;
    } else {
        path_segment_t file;
        path_walk_last(&path, &file);

        // Artificially normalize the file name
        const char end_char = *file.end;
        *((char *)file.end) = '\0';

        ret = parent->operations->create(parent, file.start, type);

        // Do not modify the original string
        *((char *)file.end) = end_char;
    }

    vfs_vnode_release(parent);
    return ret;
}

error_t vfs_remove_at(const char *raw_path)
{
    error_t ret;
    path_t path = NEW_DYNAMIC_PATH(raw_path);
    vnode_t *parent = vfs_find_parent(&path);

    if (IS_ERR(parent))
        return ERR_FROM_PTR(parent);

    if (parent->operations->remove == NULL) {
        ret = E_NOT_IMPLEMENTED;
    } else {
        path_segment_t file;
        path_walk_last(&path, &file);

        // Artificially normalize the file name
        const char end_char = *file.end;
        *((char *)file.end) = '\0';

        ret = parent->operations->remove(parent, file.start);

        // Do not modify the original string
        *((char *)file.end) = end_char;
    }

    vfs_vnode_release(parent);
    return ret;
}

struct file *vfs_open_at(const char *raw_path)
{
    vnode_t *vnode = vfs_find_by_path(raw_path);

    if (IS_ERR(vnode))
        return (void *)vnode;

    if (!vnode->operations->open)
        return PTR_ERR(E_NOT_SUPPORTED);

    return vnode->operations->open(vnode);
}

vnode_t *vfs_vnode_acquire(vnode_t *node, bool *new)
{
    if (node == NULL) {
        node = kcalloc(1, sizeof(vnode_t), KMALLOC_KERNEL);
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
