#include <kernel/execfmt.h>
#include <kernel/kmalloc.h>

#include <utils/container_of.h>

static DECLARE_LLIST(registered_execfmt);

static inline struct execfmt *to_execfmt(node_t *this)
{
    return container_of(this, struct execfmt, this);
}

error_t execfmt_register(struct execfmt *execfmt)
{
    if (!execfmt->match || !execfmt->execute)
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

    match = llist_find_first(registered_execfmt, data, __execfmt_match);
    if (match == NULL)
        return PTR_ERR(E_NOENT);

    return to_execfmt(match);
}

static struct executable *executable_new(const struct execfmt *fmt)
{
    struct executable *executable;

    executable = kcalloc(1, sizeof(*executable), KMALLOC_KERNEL);
    if (executable == NULL)
        return PTR_ERR(E_NOMEM);

    executable->fmt = fmt;

    return executable;
}

error_t execfmt_execute(void *data)
{
    const struct execfmt *fmt;
    struct executable *executable;
    error_t ret = E_SUCCESS;

    fmt = execfmt_find_matching(data);
    if (IS_ERR(fmt))
        return ERR_FROM_PTR(fmt);

    executable = executable_new(fmt);
    if (IS_ERR(executable))
        return ERR_FROM_PTR(executable);

    if (fmt->load) {
        /* The executable may be partially loaded when failing.
         * We need to release the loaded content when failing. */
        ret = fmt->load(executable, data);
        if (ret)
            goto release_executable;
    }

    ret = fmt->execute(executable);

release_executable:
    if (fmt->unload)
        fmt->unload(executable);

    kfree(executable);
    return ret;
}
