/*
 * Pipe pseudo filesystem (POSIX.1-2024)
 */

#define LOG_PREFIX "pipe"

#include <kernel/init.h>
#include <kernel/memory/slab.h>
#include <kernel/kmalloc.h>
#include <kernel/process.h>
#include <kernel/vfs.h>
#include <sys/signal.h>

#include <dailyrun/pipe.h>
#include <libalgo/ringbuffer.h>

#include <stdio.h>
#include <limits.h>
#include <unistd.h>

static struct kmem_cache *pipe_cache;

struct pipe_end {
    struct vnode *vnode;
    struct pipe *pipe;
    unsigned int which; /* read or write? */
    bool closed;
};

struct pipe {
    struct pipe_end ends[2];
    struct ringbuffer buffer; /* protected by the vnode's lock */
};

#define PIPE_BUFFER_SIZE PAGE_SIZE

/*
 *
 */
static struct pipe *pipe_alloc(void)
{
    return kmem_cache_alloc(pipe_cache, 0);
}

/*
 *
 */
static void pipe_constructor(void *obj)
{
    struct pipe *pipe = obj;

    memset(pipe, 0, sizeof(*pipe));
}

/*
 *
 */
static void pipe_free(struct pipe *pipe)
{
    kmem_cache_free(pipe_cache, pipe);
}

/*
 * Free a pipe's content.
 *
 * Called by the slab API during release.
 */
static void pipe_destructor(void *obj)
{
    struct pipe *pipe = obj;

    /* reuse the buffer if possible. */
    kfree(pipe->buffer.buf_start);
}

/*
 * Free a pipe.
 *
 * This function must only be called from the error path of the pipe's init
 * function, where we must also release the vnodes. In the normal code path
 * the pipe is released inside the vnode's release function, and the individual
 * vnodes are released inside vnode_release().
 */
static void pipe_destroy(struct pipe *pipe)
{
    if (pipe->ends[PIPE_READ].vnode) {
        vnode_free(pipe->ends[PIPE_READ].vnode);
        pipe->ends[PIPE_READ].vnode = NULL;
    }

    if (pipe->ends[PIPE_WRITE].vnode) {
        vnode_free(pipe->ends[PIPE_WRITE].vnode);
        pipe->ends[PIPE_WRITE].vnode = NULL;
    }

    pipe_free(pipe);
}

/*
 * Close one end of a pipe.
 */
static void pipe_vnode_release(struct vnode *vnode)
{
    struct pipe_end *end = vnode->pdata;
    struct pipe *pipe = end->pipe;

    end->closed = true;
    end->vnode = NULL;

    /* no users left for this pipe, we can release it */
    if (pipe->ends[PIPE_WRITE].closed &&
        pipe->ends[PIPE_READ].closed)
        pipe_free(pipe);
}

static struct vnode_operations pipe_vnops = {
    .release = pipe_vnode_release,
};

/*
 * Initialize one end of a pipe.
 */
static error_t pipe_end_init(struct pipe *pipe, struct pipe_end *end,
                             int which)
{
    struct vnode *vnode;
    struct stat *stats;
    struct user_creds *creds = NULL;

    vnode = vnode_new();
    if (IS_ERR(vnode))
        return ERR_FROM_PTR(vnode);

    end->vnode = vnode;
    end->pipe = pipe;
    end->which = which;
    end->closed = false;

    vnode->type = VNODE_FIFO;
    vnode->pdata = end;
    vnode->operations = &pipe_vnops;

    stats =  &vnode->stat;
    memset(stats, 0, sizeof(*stats));
    creds = creds_get(current->process->creds);
    stats->st_gid = creds->egid;
    stats->st_uid = creds->euid;
    creds_put(creds);

    return E_SUCCESS;
}

/*
 *
 */
static error_t pipe_init(struct pipe *pipe)
{
    void *buffer;
    error_t err;

    err = pipe_end_init(pipe, &pipe->ends[PIPE_READ], PIPE_READ);
    if (err)
        return err;

    err = pipe_end_init(pipe, &pipe->ends[PIPE_WRITE], PIPE_WRITE);
    if (err)
        return err;

    /* fast-path: pipe allocated from cache.
     * The buffer was already allocated by the previous user.
     */
    if (pipe->buffer.buf_start) {
        ringbuffer_reset(&pipe->buffer);
        return E_SUCCESS;
    }

    buffer = kmalloc(PIPE_BUFFER_SIZE, KMALLOC_KERNEL);
    if (!buffer) {
        kmem_cache_free(pipe_cache, pipe);
        return E_NOMEM;
    }

    ringbuffer_init(&pipe->buffer, buffer, PIPE_BUFFER_SIZE);

    return E_SUCCESS;
}

/*
 *
 */
static error_t pipe_open(struct file *file)
{
    file_accessed(file);
    file_modified(file);
    file_changed(file);

    return E_SUCCESS;
}

/*
 * Write into one end of a pipe.
 *
 * TODO: !O_NONBLOCK
 *
 *   We currently do not support blocking operations, this function behaves
 *   as if O_NONBLOCK is set.
 */
static ssize_t pipe_write(struct file *file, const char *buffer, size_t size)
{
    struct vnode *vnode = file->vnode;
    struct pipe_end *end = vnode->pdata;
    struct pipe *pipe = end->pipe;
    size_t remaining;

    if (end->which != PIPE_WRITE)
        return -E_PIPE;

    /* pipe has no reader */
    if (pipe->ends[PIPE_READ].closed) {
        siginfo_t sigpipe = { .si_signo = SIGPIPE };
        signal_process(current->process, &sigpipe);
        return -E_PIPE;
    }

    remaining = ringbuffer_available(&pipe->buffer);
    if (remaining < size) {
        if (size <= PIPE_BUF || remaining == 0)
            return -E_AGAIN;
        /* transfer what we can */
        size = remaining;
    }

    return ringbuffer_push(&pipe->buffer, (const u8 *)buffer, size);
}

/*
 * Read from one end of a pipe.
 *
 * TODO: !O_NONBLOCK
 *
 *   We currently do not support blocking operations, this function behaves
 *   as if O_NONBLOCK is set.
 */
static ssize_t pipe_read(struct file *file, char *buffer, size_t size)
{
    struct vnode *vnode = file->vnode;
    struct pipe_end *end = vnode->pdata;
    struct pipe *pipe = end->pipe;
    size_t remaining;

    if (end->which != PIPE_READ)
        return -E_PIPE;

    remaining = ringbuffer_remaining(&pipe->buffer);
    if (remaining == 0) {
        if (pipe->ends[PIPE_WRITE].closed)
            return 0; /* EOF */
        return -E_AGAIN;
    }

    if (remaining > size)
        remaining = size;

    return ringbuffer_pop(&pipe->buffer, (u8 *)buffer, remaining);
}

static struct file_operations pipe_fops = {
    .open = pipe_open,
    .write = pipe_write,
    .read = pipe_read,
    .seek = default_file_seek,
};

/*
 *
 */
int sys_pipe(int *fds)
{
    struct pipe *pipe;
    struct file *read_file = NULL;
    struct file *write_file = NULL;
    int ret;

    pipe = pipe_alloc();
    if (!pipe)
        return -E_NOMEM;

    ret = -pipe_init(pipe);
    if (ret)
        goto exit_error;

    read_file = file_open(pipe->ends[PIPE_READ].vnode, &pipe_fops);
    if (IS_ERR(read_file)) {
        ret = -ERR_FROM_PTR(read_file);
        goto exit_error;
    }

    fds[PIPE_READ] = process_add_fd(current->process, read_file, FD_RW);
    if (fds[PIPE_READ] < 0) {
        ret = fds[PIPE_READ];
        goto exit_error;
    }

    write_file = file_open(pipe->ends[PIPE_WRITE].vnode, &pipe_fops);
    if (IS_ERR(write_file)) {
        ret = -ERR_FROM_PTR(write_file);
        goto exit_error;
    }

    fds[PIPE_WRITE] = process_add_fd(current->process, write_file, FD_RW);
    if (fds[PIPE_WRITE] < 0) {
        ret = fds[PIPE_WRITE];
        process_remove_fd(current->process, fds[PIPE_READ]);
        read_file = NULL; /* file_put() called by process_unregister_file(). */
        goto exit_error;
    }

    return 0;

exit_error:
    if (write_file)
        file_close(write_file);
    if (read_file)
        file_close(read_file);
    pipe_destroy(pipe);
    return ret;
}

static error_t pipe_api_init(void)
{
    pipe_cache = kmem_cache_create("pipe", sizeof(struct pipe), CPU_CACHE_ALIGN,
                                   pipe_constructor, pipe_destructor);
    PANIC_ON(!pipe_cache, "failed to init cache: pipe");

    return E_SUCCESS;
}

DECLARE_INITCALL(INIT_NORMAL, pipe_api_init);
