#define LOG_DOMAIN "execfmt"

#include <kernel/execfmt.h>
#include <kernel/file.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/mmu.h>
#include <kernel/process.h>

#include <utils/container_of.h>
#include <utils/math.h>

static DECLARE_LLIST(registered_execfmt);

static inline struct execfmt *to_execfmt(node_t *this)
{
    return container_of(this, struct execfmt, this);
}

error_t execfmt_register(struct execfmt *execfmt)
{
    if (!execfmt->match || !execfmt->load)
        return E_INVAL;

    llist_add(&registered_execfmt, &execfmt->this);

    return E_SUCCESS;
}

static int __execfmt_match(const void *match, const void *data)
{
    const struct execfmt *execfmt = match;
    return execfmt->match(data) ? COMPARE_EQ : !COMPARE_EQ;
}

static const struct execfmt *execfmt_find_matching(void *data)
{
    node_t *match = NULL;

    match = llist_find_first(&registered_execfmt, data, __execfmt_match);
    if (match == NULL)
        return PTR_ERR(E_NOENT);

    return to_execfmt(match);
}

static struct executable *executable_new(void)
{
    struct executable *executable;

    executable = kcalloc(1, sizeof(*executable), KMALLOC_KERNEL);
    if (executable == NULL)
        return PTR_ERR(E_NOMEM);

    return executable;
}

NO_RETURN void execfmt_execute_executable(struct executable *executable)
{
    /*
     * Running an executable from kernel space makes no sense. This should
     * always be called in the context of a call to execve.
     */
    thread_jump_to_userland(executable->entrypoint, NULL);
}

error_t execfmt_execute(struct file *exec_file)
{
    const struct execfmt *fmt;
    struct executable *executable;
    error_t ret = E_SUCCESS;
    void *content;

    /*
     * The kernel cannot be allowed to run external executables.
     * It should instead create user processes and make them run the executable
     * in its stead (e.g. the init process).
     */
    if (unlikely(current->process == &kernel_process)) {
        log_err("kernel process cannot execute executables\n");
        return E_PERM;
    }

    content = map_file(exec_file, PROT_READ);
    if (content == MMAP_INVALID) {
        log_err("failed to read 'busybox'");
        return E_NOMEM;
    }

    fmt = execfmt_find_matching(content);
    if (IS_ERR(fmt))
        return ERR_FROM_PTR(fmt);

    executable = executable_new();
    if (IS_ERR(executable)) {
        ret = ERR_FROM_PTR(executable);
        goto release_executable;
    }

    ret = address_space_clear(current->process->as);
    if (ret) {
        log_err("failed to clear address space: %s", err_to_str(ret));
        goto release_executable;
    }

    if (fmt->load) {
        ret = fmt->load(executable, content);
        if (ret) {
            log_err("failed to load executable: %s\n", err_to_str(ret));
            goto release_executable;
        }
    }

    /*
     * TODO: Refactor this once to be a simple reti when implementing execve.
     *       For now we simply jump to userland for testing purposes, in order
     *       to be able to execute our first binary.
     */
    if (executable->entrypoint) {
        execfmt_execute_executable(executable);
        assert_not_reached();
    }

    log_err("executable has no entrypoint");

release_executable:
    /* FIXME: The executable may be partially loaded when failing.
     * We need to release the loaded content when failing.
     */
    unmap_file(exec_file, content);
    if (!IS_ERR(content))
        kfree(executable);
    return ret;
}
