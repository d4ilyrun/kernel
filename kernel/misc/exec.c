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

static NO_RETURN void execfmt_execute_executable(struct executable *executable,
                                                 void *stack_pointer,
                                                 void *base_pointer)
{
    thread_jump_to_userland(stack_pointer, base_pointer, executable->entrypoint,
                            NULL);
}

#define stack_push(top, bottom, item)      \
    ({                                     \
        (top) -= sizeof(item);             \
        if (top < bottom)                  \
            top = PTR_ERR(E_TOO_BIG);      \
        else                               \
            *(typeof(item) *)(top) = item; \
        top;                               \
    })

/*
 *
 */
static void *execfmt_push_string(const char *string, void *stack_top,
                                 void *stack_bottom, size_t *pushed)
{
    size_t len;

    /* We also copy the NUL terminating byte. */
    len = strnlen(string, EXECFMT_MAX_ARG_SIZE) + 1;

    stack_top -= len;
    if (stack_top < stack_bottom)
        return PTR_ERR(E_TOO_BIG);

    memcpy(stack_top, string, len);
    *pushed += len;

    return stack_top;
}

/*
 *
 */
static void *execfmt_push_strings(char *const strings[], void *stack_top,
                                  void *stack_bottom, size_t count,
                                  size_t *pushed)
{
    char *str;

    /*
     * Push the arg strings onto the user stack.
     */
    while (count-- > 0) {
        str = strings[count];
        stack_top = execfmt_push_string(str, stack_top, stack_bottom, pushed);
        if (IS_ERR(stack_top))
            return stack_top;
    }

    return stack_top;
}

/*
 * Save the executable's parameters into a temporary stack.
 *
 * Once pushed, the parameters can be popped back onto the actual user stack
 * using execfmt_pop_params().
 *
 * This function only pushes the content of the string arguments. Constructing
 * the corresponding array of char * is done by execfmt_pop_strings() when
 * popping the parameters back onto the user stack.
 */
static void *execfmt_push_params(struct exec_params *params, void *stack_top,
                                 void *stack_bottom, size_t *argv_size,
                                 size_t *envp_size)
{
    size_t pushed = 0;

    stack_top = execfmt_push_strings(params->envp, stack_top, stack_bottom,
                                     params->envpc, &pushed);
    if (IS_ERR(stack_top))
        return stack_top;
    params->envp = stack_top;
    *envp_size = pushed;

    stack_top = execfmt_push_strings(params->argv, stack_top, stack_bottom,
                                     params->argc, &pushed);
    if (IS_ERR(stack_top))
        return stack_top;
    params->argv = stack_top;
    *argv_size = pushed - *envp_size;

    return stack_top;
}

/*
 * Pop concatenated argument strings from the temporary stack and construct
 * the corresponding array of char * pointer.
 *
 * @param strings The beginning of the concatenated strings pushed by
 *                execfmt_push_strings().
 * @param size    The size of the conctenated strings array.
 *
 * @return The new stack top after popping the strings (the begginning of the
 *         constructed char * array)
 */
static void *execfmt_pop_strings(char *strings, void *stack_top,
                                 void *stack_bottom, size_t size)
{
    void *new_top = stack_top - size;

    if (IS_ERR(stack_push(new_top, stack_bottom, NULL)))
        return stack_top;

    if (size == 0)
        return new_top;

    /* Copy final \0 character to avoid a false positive during the loop. */
    stack_push(stack_top, stack_bottom, strings[--size]);

    while (size-- > 0) {
        stack_push(stack_top, stack_bottom, strings[size]);
        if (strings[size] == '\0') {
            if (IS_ERR(stack_push(new_top, stack_bottom, stack_top + 1)))
                return stack_top;
        }
    }

    return stack_push(new_top, stack_bottom, stack_top);
}

/*
 * Pop the executable's parameters from the temporary stack.
 *
 * This follows the System-V ABI. Argument strings are pushed onto the user
 * stack, followed by an array of pointers to each individual string.
 * Pointers to each array (argv, envp) and the number of elements inside argv
 * are pushed last so that they can be used like regular arguments by _start().
 *
 * - envp_strings
 * - [envp]
 * - argv_strings
 * - [argv]
 * - &envp
 * - &argv
 * - argc
 * - stack base pointer (ebp) <--- user stack pointer
 *
 * @return The user stack pointer after pushing all values
 */
static void *execfmt_pop_params(const struct exec_params *params,
                                void *stack_top, void *stack_bottom,
                                size_t argv_size, size_t envp_size)
{
    void *envp;
    void *argv;

    stack_top = execfmt_pop_strings((char *)params->envp, stack_top,
                                    stack_bottom, envp_size);
    if (IS_ERR(stack_top))
        return stack_top;
    envp = stack_top;

    stack_top = execfmt_pop_strings((char *)params->argv, stack_top,
                                    stack_bottom, argv_size);
    if (IS_ERR(stack_top))
        return stack_top;
    argv = stack_top;

    if (IS_ERR(stack_push(stack_top, stack_bottom, envp)))
        return stack_top;
    if (IS_ERR(stack_push(stack_top, stack_bottom, argv)))
        return stack_top;
    if (IS_ERR(stack_push(stack_top, stack_bottom, (int)params->argc)))
        return stack_top;

    /* Reset stack base pointer. */
    return stack_push(stack_top, stack_bottom, NULL);
}

error_t execfmt_execute(struct exec_params *params)
{
    struct file *exec_file;
    const struct execfmt *fmt;
    struct executable *executable;
    void *args_buffer = NULL;
    void *args_buffer_bottom;
    size_t argv_size;
    size_t envp_size;
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

    /*
     * Push the executable arguments into the kernel adress space so they can
     * be popped back onto the user stack after the current address space has
     * been cleared.
     */

    args_buffer = vm_alloc(&kernel_address_space, EXECFMT_ARGS_BUFFER_SIZE,
                           VM_KERNEL_RW);
    if (!args_buffer) {
        log_err("failed to allocate exec args buffer");
        ret = E_NOMEM;
        goto release_executable;
    }

    args_buffer_bottom = args_buffer + EXECFMT_ARGS_BUFFER_SIZE;
    args_buffer_bottom = execfmt_push_params(params, args_buffer_bottom,
                                             args_buffer, &argv_size,
                                             &envp_size);
    if (IS_ERR(args_buffer_bottom)) {
        ret = ERR_FROM_PTR(args_buffer_bottom);
        goto release_executable;
    }

    /*
     * Rename the new process to match the name of the executable file.
     */
    path = NEW_DYNAMIC_PATH(params->exec_path);
    path_walk_last(&path, &segment);
    process_set_name(current->process, segment.start,
                     path_segment_length(&segment));

    ret = address_space_clear(current->process->as);
    if (ret) {
        log_err("failed to clear address space: %pe", &ret);
        goto release_executable;
    }

    can_return = false;

    ret = address_space_init(current->process->as);
    if (ret) {
        log_err("failed to re-init the address space: %pe", &ret);
        goto release_executable;
    }

    if (fmt->load) {
        ret = fmt->load(executable, content);
        if (ret) {
            log_err("failed to load executable: %pe", &ret);
            goto release_executable;
        }
    }

    ustack = vm_alloc(current->process->as, USER_STACK_SIZE,
                      VM_USER_RW | VM_CLEAR);
    thread_set_user_stack(current, ustack);

    /*
     * Pop the previously pushed arguments out of the temporary kernel buffer
     * onto the newly created user stack.
     */
    ustack = execfmt_pop_params(params, thread_get_user_stack_top(current),
                                thread_get_user_stack(current),
                                argv_size, envp_size);
    if (IS_ERR(ustack)) {
        ret = ERR_FROM_PTR(ustack);
        log_err("failed to pop executable params: %s", err_to_str(ret));
        goto release_executable;
    }

    vm_free(&kernel_address_space, args_buffer);
    args_buffer = NULL;

    if (executable->entrypoint) {
        execfmt_execute_executable(executable, ustack, ustack);
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
    vm_free(kernel_process.as, args_buffer);

release_executable_file:
    file_put(exec_file);

    if (!can_return) {
        thread_kill(current);
        assert_not_reached();
    }

    return ret;
}

static size_t execfmt_count_args(char * const args[])
{
    for (size_t count = 0; count <= EXECFMT_MAX_ARGS; ++count) {
        if (args[count] == NULL)
            return count;
    }

    return EXECFMT_MAX_ARGS + 1;
}

int sys_execve(const char *path, char *const argv[], char *const envp[])
{
    struct exec_params params;
    error_t ret;

    params.exec_path = path;
    params.argv = argv;
    params.argc = execfmt_count_args(argv);
    params.envp = envp;
    params.envpc = execfmt_count_args(envp);

    if (params.argc > EXECFMT_MAX_ARGS || params.envpc > EXECFMT_MAX_ARGS)
        return -E_TOO_BIG;

    ret = execfmt_execute(&params);
    if (ret)
        return -ret;

    return 0;
}
