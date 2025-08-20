/**
 * @defgroup kernel_execfmt Executable file format
 * @ingroup kernel
 *
 * # Executable file format
 *
 * Similarily to the Linux kernel's, the binary format (execfmt) API
 * is used to define supported executable file format.
 *
 * When executing a file from userland, it is matched against all
 * registered execfmt entries. If no matching binary format is found
 * the file is deemed 'unexecutable'. Else, the execfmt's callback
 * are used to parse/load the file in memory (if necessary), and
 * execute it.
 *
 * @see Linux kernel's execfmt API
 *
 * @{
 */
#ifndef KERNEL_EXECFMT_H
#define KERNEL_EXECFMT_H

#include <kernel/error.h>
#include <kernel/types.h>

#include <libalgo/linked_list.h>

struct executable;
struct file;

/** An executable file format */
struct execfmt {
    node_t this;
    const char *name;            ///< Format's name
    bool (*match)(const void *); ///< Check whether an executable file is of the
                                 ///< given format
    error_t (*load)(struct executable *, void *); ///< Load an executable file
                                                  ///< in memory
};

/** Represents a loaded executable
 *
 *  When a matching executable file format is found, an executable
 *  may be loaded in memory (if necessary). This struct contains the
 *  loaded executable's internal context (allocated memory addresses ...).
 */
struct executable {
    void (*entrypoint)(void*);
};

/** Executable parameters.
 *
 * These parameters are pushed onto the stack to be passed as arguments to the
 * executable's entrypoint function (main).
 *
 * The argument strings must be concatenated and separated with a NULL byte
 * inside the argv buffer. This is also the case for envp.
 */
struct exec_params {
    const char *exec_path;
    int argc;
    const char *argv; /*!< Contains the concatenated argument strings */
    size_t argv_size; /*!< Total size of the @argv buffer */
    const char *envp; /*!< Contains the concatenated environnement variables */
    size_t envp_size; /*!< Total size of the @envp buffer */
};

/** Register a new executable file format */
error_t execfmt_register(struct execfmt *);

/** Execute a given executable file's content
 *
 * Prior to being executed, the file is matched against
 * all known (aka supported) executable file formats, and
 * loaded into memory. If any of these steps fail, this
 * function returns the failing step's error value.
 */
error_t execfmt_execute(const struct exec_params *);

#endif /* KERNEL_EXECFMT_H */

/** @} */
