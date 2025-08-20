#define LOG_DOMAIN "execfmt"

#include <kernel/execfmt.h>
#include <kernel/file.h>
#include <kernel/kmalloc.h>
#include <kernel/logger.h>
#include <kernel/mmu.h>
#include <kernel/process.h>
#include <kernel/vfs.h>

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

static NO_RETURN void execfmt_execute_executable(struct executable *executable)
{
    thread_jump_to_userland(executable->entrypoint, NULL);
}

#define stack_push(top, item)          \
    do {                               \
        (top) -= sizeof(item);         \
        *(typeof(item) *)(top) = item; \
    } while (0)

static void *execfmt_push_array(void *stack_top, const char *array, size_t size)
{
    char *old_top = stack_top;
    char *new_top;

    stack_top -= size;
    memcpy(stack_top, array, size);

    old_top -= 2;
    new_top = stack_top;

    stack_push(stack_top, NULL);

    if (size == 0)
        return stack_top;

    while (old_top > new_top) {
        if (*old_top == '\0')
            stack_push(stack_top, old_top);
        old_top -= 1;
    }

    stack_push(stack_top, new_top);

    return stack_top;
}

/*
 * As specified in the system-v ABI, arguments to the executable (argc, argv,
 * envp) are passed through the stack are placed at the very top of the user
 * stack, followed by pointers to
 * the beginning of each sections.
 *
 * - [envp]
 * - [argv]
 * - &envp
 * - &argv
 * - argc
 * - stack base pointer (ebp) <--- user stack pointer
 *
 * This function assumes that the content of the exec_params structure has
 * already been sanitized before.
 *
 * @return The user stack pointer after pushing all values
 */
static void *
execfmt_push_params(void *stack_top, const struct exec_params *params)
{
    void *envp;
    void *argv;

    stack_top = execfmt_push_array(stack_top, params->envp, params->envp_size);
    envp = stack_top;

    stack_top = execfmt_push_array(stack_top, params->argv, params->argv_size);
    argv = stack_top;

    stack_push(stack_top, envp);
    stack_push(stack_top, argv);
    stack_push(stack_top, (int)params->argc);

    /* Reset stack base pointer. */
    stack_push(stack_top, (void *)0);

    return stack_top;
}

error_t execfmt_execute(const struct exec_params *params)
{
    struct file *exec_file;
    const struct execfmt *fmt;
    struct executable *executable;
    error_t ret = E_SUCCESS;
    void *content;
    void *ustack;
    bool can_return = true;
    path_segment_t segment;
    path_t path;

    /*
     * The kernel cannot be allowed to run external executables.
     * It should instead create user processes and make them run the executable
     * in its stead (e.g. the init process).
     */
    if (unlikely(current->process == &kernel_process)) {
        log_err("kernel process cannot execute executables\n");
        return E_PERM;
    }

    exec_file = vfs_open(params->exec_path, O_RDONLY);
    if (IS_ERR(exec_file))
        return ERR_FROM_PTR(exec_file);

    content = map_file(exec_file, PROT_READ);
    if (content == MMAP_INVALID) {
        ret = E_NOMEM;
        goto release_executable_file;
    }

    fmt = execfmt_find_matching(content);
    if (IS_ERR(fmt)) {
        ret = ERR_FROM_PTR(fmt);
        goto release_executable;
    }

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

    can_return = false;

    ret = address_space_init(current->process->as);
    if (ret) {
        log_err("failed to re-init the address space: %s", err_to_str(ret));
        goto release_executable;
    }

    if (fmt->load) {
        ret = fmt->load(executable, content);
        if (ret) {
            log_err("failed to load executable: %s", err_to_str(ret));
            goto release_executable;
        }
    }

    /*
     * Rename the new process to match the name of the executable file.
     */
    path = NEW_DYNAMIC_PATH(params->exec_path);
    path_walk_last(&path, &segment);
    process_set_name(current->process, segment.start,
                     path_segment_length(&segment));

    ustack = vm_alloc(current->process->as, USER_STACK_SIZE,
                      VM_USER_RW | VM_CLEAR);
    thread_set_user_stack(current, ustack);

    ustack = execfmt_push_params(thread_get_user_stack_top(current), params);
    thread_set_stack_pointer(current, ustack);

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

release_executable_file:
    file_put(exec_file);

    if (!can_return) {
        thread_kill(current);
        assert_not_reached();
    }

    return ret;
}
