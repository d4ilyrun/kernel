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
    struct vnode vnode;
    unsigned int which; /* read or write? */
    bool closed;
};

struct pipe {
    struct pipe_end ends[2];
    struct ringbuffer buffer; /* protected by the vnode's lock */
};

#define PIPE_BUFFER_SIZE PAGE_SIZE

static inline struct pipe_end *pipe_end(struct vnode *vnode)
{
    return container_of(vnode, struct pipe_end, vnode);
}

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
 *
 */
static error_t pipe_init(struct pipe *pipe)
{
    void *buffer;

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
 * Open and initialize one end of a pipe.
 */
static error_t pipe_open(struct file *file)
{
    struct vnode *vnode = file->vnode;
    struct stat *stat = &vnode->stat;
    struct user_creds *creds = NULL;

    memset(stat, 0, sizeof(*stat));

    vnode->type = VNODE_FIFO;

    file_accessed(file);
    file_modified(file);
    file_changed(file);

    creds = creds_get(current->process->creds);
    stat->st_gid = creds->egid;
    stat->st_uid = creds->euid;
    creds_put(creds);

    return E_SUCCESS;
}

/*
 * Close one end of a pipe.
 */
static void pipe_close(struct file *file)
{
    struct vnode *vnode = file->vnode;
    struct pipe_end *end = pipe_end(vnode);
    struct pipe *pipe = vnode->pdata;

    end->closed = true;

    /* no users left for this pipe, we can release it */
    if (pipe->ends[PIPE_WRITE].closed &&
        pipe->ends[PIPE_READ].closed)
        pipe_free(pipe);
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
    struct pipe_end *end = pipe_end(vnode);
    struct pipe *pipe = vnode->pdata;
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
    struct pipe_end *end = pipe_end(vnode);
    struct pipe *pipe = vnode->pdata;
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
    .close = pipe_close,
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
    struct pipe_end *read_end;
    struct pipe_end *write_end;
    struct file *read_file = NULL;
    struct file *write_file = NULL;
    int ret;

    pipe = pipe_alloc();
    if (!pipe)
        return -E_NOMEM;

    ret = -pipe_init(pipe);
    if (ret)
        goto exit_error;

    read_end = &pipe->ends[PIPE_READ];
    read_end->which = PIPE_READ;
    read_end->vnode.pdata = pipe;

    read_file = file_open(&read_end->vnode, &pipe_fops);
    if (IS_ERR(read_file)) {
        ret = -ERR_FROM_PTR(read_file);
        goto exit_error;
    }

    fds[PIPE_READ] = process_register_file(current->process, read_file);
    if (fds[PIPE_READ] < 0) {
        ret = fds[PIPE_READ];
        goto exit_error;
    }

    write_end = &pipe->ends[PIPE_WRITE];
    write_end->which = PIPE_WRITE;
    write_end->vnode.pdata = pipe;

    write_file = file_open(&write_end->vnode, &pipe_fops);
    if (IS_ERR(write_file)) {
        ret = -ERR_FROM_PTR(write_file);
        goto exit_error;
    }

    fds[PIPE_WRITE] = process_register_file(current->process, write_file);
    if (fds[PIPE_WRITE] < 0) {
        ret = fds[PIPE_WRITE];
        process_unregister_file(current->process, fds[PIPE_READ]);
        read_file = NULL; /* file_put() called by process_unregister_file(). */
        goto exit_error;
    }

    return 0;

exit_error:
    if (write_file)
        file_close(write_file);
    if (read_file)
        file_close(read_file);
    pipe_free(pipe);
    return ret;
}

static error_t pipe_api_init(void)
{
    pipe_cache = kmem_cache_create("pipe", sizeof(struct pipe), CPU_CACHE_ALIGN,
                                   NULL, pipe_destructor);
    PANIC_ON(!pipe_cache, "failed to init cache: pipe");

    return E_SUCCESS;
}

DECLARE_INITCALL(INIT_NORMAL, pipe_api_init);
