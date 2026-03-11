/*
 * Reference counting API.
 *
 * By definition a reference count is always a positive value. The object should
 * be freed when the count reaches 0 (by means of refcnt_put()), and any further
 * attempts to increase this count will panic.
 */

#ifndef KERNEL_REFCNT_H
#define KERNEL_REFCNT_H

#include <kernel/atomic.h>
#include <kernel/logger.h>

typedef atomic_t refcnt_t;

/** Initialize the reference counter.
 *
 *  @warning Do not call refcnt_get() after refcnt_init() as the latter
 *           already accounts for the initial reference. This would create
 *           dangling objects.
 */
static inline void refcnt_init(refcnt_t *ref)
{
    atomic_write(ref, 1);
}

#define REFCNT_INIT_STATIC() ((refcnt_t){.val = 1})

/** @return the current number of references to an object. */
static inline unsigned int refcnt_read(refcnt_t *ref)
{
    return atomic_read(ref);
}

/** Increase the number of references to an object. */
static inline unsigned int refcnt_get(refcnt_t *ref)
{
    unsigned int refs;

    /*
     * All references to this object have been released already.
     * Do not 'resurrect' it as it may have been scheduled to be
     * garbage collected (e.g. process API).
     */
    refs = atomic_inc(ref);
    ASSERT(refs > 0);

    return refs;
}

/** Decrease the number of references to an object.
 *
 *  @return The updated number of references.
 */
static inline unsigned int refcnt_put(refcnt_t *ref)
{
    return atomic_dec(ref) - 1;
}

#endif /* KERNEL_REFCNT_H */
