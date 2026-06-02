/*
 * Shared memory (POSIX.1-2024)
 *
 * ## Locking
 *
 * shm_mappings_lock must be held when accessing (reading or modifying)
 * the list of shared memory objects.
 *
 * ## Reference counting
 *
 * Shared memory objects live as long as their vnode. The physical pages
 * and the shm_mapping structure are released when the last reference to
 * the vnode is released.
 *
 * An extra reference is taken during creation so that the object is only
 * deleted if unlink() has been called on it. Additional references are
 * taken by:
 *
 * - Every file that references the object (regular file subsystem)
 * - Every physical page allocated in vnode_get_page() (released in
 *   vnode_put_page())
 */

#define LOG_DOMAIN "shm"

#include <kernel/process.h>
#include <kernel/error.h>
#include <kernel/file.h>
#include <kernel/init.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/spinlock.h>
#include <kernel/vfs.h>
#include <kernel/pmm.h>

#include <libalgo/linked_list.h>

#include <limits.h>
#include <string.h>

static DECLARE_LLIST(shm_mappings);
static DECLARE_SPINLOCK(shm_mappings_lock);

struct shm_mapping {
    char            name[NAME_MAX + 1];
    struct vnode   *vnode;
    struct page   **pages;
    unsigned int    pages_count;
    spinlock_t      pages_lock;

    LLIST_NODE(this);
};

static inline struct shm_mapping *to_shm(const node_t *this)
{
    return container_of(this, struct shm_mapping, this);
}

static inline const char *shm_name(const struct shm_mapping *shm)
{
    return shm->name;
}

/*
 *
 */
static struct shm_mapping *shm_alloc(void)
{
    return kcalloc(1, sizeof(struct shm_mapping), KMALLOC_KERNEL);
}

/*
 *
 */
static void shm_free(struct shm_mapping *shm)
{
    if (shm)
        kfree(shm);
}

/*
 *
 */
static void shm_destroy(struct shm_mapping *shm)
{
    for (unsigned int i = 0; i < shm->pages_count; ++i) {
        struct page *page = shm->pages[i];
        page->flags &= ~PAGE_VNODE;
        page_put(page);
    }

    kfree(shm->pages);
    shm_free(shm);
}

/*
 * Compare function used by @ref shm_find()
 */
static int shm_find_compare(const void *this, const void *data)
{
    const struct shm_mapping *shm = to_shm(this);
    return strcmp(data, shm_name(shm));
}

/*
 * Find an existing shared memory mapping.
 */
static struct shm_mapping *shm_find(const char *name)
{
    node_t *node;

    WARN_ON(!spinlock_is_held(&shm_mappings_lock));

    node = llist_find_first(&shm_mappings, name, shm_find_compare);
    if (!node)
        return NULL;

    return to_shm(node);
}

/*
 *
 */
static void shm_vnode_release(struct vnode *vnode)
{
    shm_destroy(vnode->pdata);
}

/*
 *
 */
static struct page *shm_vnode_get_page(struct vnode *vnode, off_t offset)
{
    struct shm_mapping *shm = vnode->pdata;
    unsigned int index = offset / PAGE_SIZE;
    struct page *page;
    error_t err;

    spinlock_acquire(&shm->pages_lock);

    /* Trying to get a page for an offset outside the current limits.
     * We need to resize the array of pages in order to fit this index.
     *
     * NOTE: The size of this array should be dictated by ftruncate(), and
     *       we should send a SIGBUS signal in this case.
     */
    if (index >= shm->pages_count) {
        shm->pages = krealloc_array(shm->pages, index + 1, sizeof(*shm->pages),
                                    KMALLOC_KERNEL);
        /* TODO: do not delete the old array and simply return E_NOMEM */
        PANIC_ON(shm->pages == NULL, "failed to resize shm pages array");

        memset(&shm->pages[shm->pages_count], 0,
               (index - shm->pages_count + 1) * sizeof(*shm->pages));
        shm->pages_count = index + 1;
    }

    /* First time this page is referenced, create it. */
    if (shm->pages[index] == NULL) {
        shm->pages[index] = address_to_page(pmm_allocate());
        if (!shm->pages[index]) {
            err = E_NOMEM;
            goto fail;
        }

        page = shm->pages[index];
        page->flags |= PAGE_VNODE;
        page->vn_vnode = vnode_acquire(vnode, NULL);
        page->vn_offset = offset;
    } else {
        /* NOTE: Only one reference to the page is taken (by the object itself).
         *
         * Subsequent calls to get_page() take an additional reference to
         * the object (~ vnode), so that the page is only released once it
         * is deleted (vnode_release() called when no users are left and
         * shm_unlink() has been called).
         */
        page = shm->pages[index];
        vnode_acquire(page->vn_vnode, NULL);
    }

    spinlock_release(&shm->pages_lock);
    return page;

fail:
    spinlock_release(&shm->pages_lock);
    return PTR_ERR(err);
}

/*
 *
 */
static void shm_vnode_put_page(struct vnode *vnode, struct page *page)
{
    struct shm_mapping *shm = vnode->pdata;
    unsigned int index = page->vn_offset / PAGE_SIZE;

    spinlock_acquire(&shm->pages_lock);
    ASSERT(shm->pages[index] == page);
    vnode_release(vnode);
    spinlock_release(&shm->pages_lock);
}

static struct vnode_operations shm_vnops = {
    .release = shm_vnode_release,
    .get_page = shm_vnode_get_page,
    .put_page = shm_vnode_put_page,
};

/*
 *
 */
static struct shm_mapping *shm_create(const char *name, struct user_creds *creds,
                                      mode_t mode)
{
    struct shm_mapping *shm = NULL;
    struct vnode *vnode = NULL;
    error_t err;

    ASSERT(spinlock_is_held(&shm_mappings_lock));

    err = E_NOMEM;
    vnode = vnode_alloc();
    if (!vnode)
        goto fail;

    shm = shm_alloc();
    if (!shm)
        goto fail;

    shm->vnode = vnode;
    INIT_SPINLOCK(shm->pages_lock);
    INIT_LLIST_NODE(shm->this);
    vnode_fill_stats(vnode, mode, creds);
    strlcpy(shm->name, name, sizeof(shm->name));
    vnode->operations = &shm_vnops;
    vnode->pdata = shm;

    llist_add(&shm_mappings, &shm->this);

    return shm;

fail:
    if (vnode)
        vnode_free(vnode);
    if (shm)
        shm_free(shm);
    return PTR_ERR(err);
}

static struct file_operations shm_fops = {
};

/*
 *
 */
static struct file *shm_open(const char *name, int oflags, mode_t mode)
{
    struct shm_mapping *shm;
    struct file *file;
    struct user_creds *creds;
    bool destroy_on_error = false;
    error_t err;

    spinlock_acquire(&shm_mappings_lock);
    creds = creds_get(current->process->creds);

    shm = shm_find(name);
    if (oflags & O_CREAT) {
        if (shm && oflags & O_EXCL) {
            err = E_EXIST;
            goto fail;
        }
        if (!shm) {
            shm = shm_create(name, creds, mode);
            if (IS_ERR(shm)) {
                err = ERR_FROM_PTR(shm);
                goto fail;
            }

            /* extra reference released by unlink() */
            vnode_acquire(shm->vnode, NULL);
            destroy_on_error = true;
        }
    }

    if (!shm) {
        err = E_NOENT;
        goto fail;
    }

    locked_scope(&shm->vnode->lock) {
        if (!vnode_check_creds(shm->vnode, creds, oflags)) {
            err = E_ACCESS;
            goto fail;
        }
    }

    destroy_on_error = false;
    file = file_open(shm->vnode, &shm_fops);
    if (IS_ERR(file)) {
        log_warn("failed to create file for shm object: %s", shm_name(shm));
        err = ERR_FROM_PTR(file);
        goto fail;
    }

out:
    creds_put(current->process->creds);
    spinlock_release(&shm_mappings_lock);
    return file;

fail:
    file = PTR_ERR(err);
    if (destroy_on_error)
        vnode_release(shm->vnode);
    goto out;
}

/* shm_open() syscall
 *
 * TODO: Reuse sys_open() by making shm a proper mountable filesystem.
 *       But we would need to be able to make sure that the path given
 *       to shm_open() points to a shmfs.
 *
 * POSIX.1-2024
 */
int sys_shm_open(const char *name, int oflags, mode_t mode)
{
    struct file *file;
    error_t err;
    int flags;
    int fd;

    if (strnlen(name, NAME_MAX + 1) > NAME_MAX)
        return -E_NAME_TOO_LONG;

    file = shm_open(name, oflags, mode);
    if (IS_ERR(file))
        return -ERR_FROM_PTR(file);

    err = compute_fd_flags(oflags, &flags);
    if (err) {
        file_put(file);
        return -err;
    }

    fd = process_add_fd(current->process, file, flags);
    if (fd < 0) {
        file_put(file);
        return fd;
    }

    return fd;
}

/* shm_unlink() syscall.
 *
 * Remove a shared object.
 *
 * NOTE: the actual memory object is not destroyed until the last close
 *       and unmap on it have occurred if it is already in use.
 *
 * POSIX.1-2024
 */
int sys_shm_unlink(const char *name)
{
    struct shm_mapping *shm;

    if (strnlen(name, NAME_MAX + 1) > NAME_MAX)
        return -E_NAME_TOO_LONG;

    spinlock_acquire(&shm_mappings_lock);
    shm = shm_find(name);
    if (shm != NULL)
        llist_remove(&shm->this);
    spinlock_release(&shm_mappings_lock);

    if (!shm)
        return -E_NOENT;

    /* release extra reference */
    vnode_release(shm->vnode);

    return 0;
}
