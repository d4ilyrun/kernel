#ifndef KERNEL_ATOMIC_H
#define KERNEL_ATOMIC_H

#ifdef __STDC_NO_ATOMICS__
#error No standard atomic header available, are you compiling using C11 at least ?
#endif

#include <kernel/types.h>

#include <utils/compiler.h>

#include <stdatomic.h>

#define __atomic_t native_t

/** An atomic value.
 *
 *  This value has been placed inside a struct to enforce using the
 *  atomic_*() set of functions when accessing it. Please do not use
 *  this field directly.
 */
typedef struct {
    _Atomic __atomic_t val; /** The underlying value */
} atomic_t;

static_assert(sizeof(atomic_t) == sizeof(__atomic_t));
static_assert(offsetof(atomic_t, val) == 0);

/** Cast a compatible scalar type into an atomic type. */
#define atomic_cast(val)                                                  \
    ({                                                                    \
        _Static_assert(same_size(atomic_t, *val), "Invalid atomic cast"); \
        (atomic_t *)(val);                                                \
    })

/** Cast a compatible scalar type into an constant atomic type. */
#define const_atomic_cast(val)                                            \
    ({                                                                    \
        _Static_assert(same_size(atomic_t, *val), "Invalid const atomic " \
                                                  "cast");                \
        (const atomic_t *)(val);                                          \
    })

/** Atomically read the content of a scalar variable */
#define READ_ONCE(_x)                                                         \
    ({                                                                        \
        _Static_assert(is_native_word(_x), "Variable is not of native size"); \
        *(const volatile typeof(_x) *)&(_x);                                  \
    })

/** Atomically modify the content of a scalar variable */
#define WRITE_ONCE(_x, _val)                                                  \
    ({                                                                        \
        _Static_assert(is_native_word(_x), "Variable is not of native size"); \
        *(volatile typeof(_x) *)&(_x) = (_val);                               \
    })

#define stdatomic_read atomic_load
#define stdatomic_write atomic_store
#define stdatomic_add atomic_fetch_add
#define stdatomic_sub atomic_fetch_sub

/*
 * Defined inside stdatomic.h, but we need to redefine it ourselves to be able
 * to reuse the atomic_exchange() name. This definition is taken straight from
 * the original stdatomic.h implementation of atomic_exchange.
 */
#undef atomic_exchange
#define stdatomic_exchange(atomic, val) \
    atomic_exchange_explicit(atomic, val, memory_order_seq_cst);

/** Read the value of an atomic variable */
static ALWAYS_INLINE __atomic_t atomic_read(const atomic_t *atomic)
{
    return stdatomic_read(&atomic->val);
}

/** Modify an atomic variable's value */
static ALWAYS_INLINE void atomic_write(atomic_t *atomic, __atomic_t val)
{
    stdatomic_write(&atomic->val, val);
}

/** Modify an atomic variable's value and return the old one */
static ALWAYS_INLINE __atomic_t atomic_exchange(atomic_t *atomic,
                                                __atomic_t val)
{
    return stdatomic_exchange(&atomic->val, val);
}

/** Increment the value of an atomic variable */
static ALWAYS_INLINE void atomic_add(atomic_t *atomic, __atomic_t val)
{
    stdatomic_add(&atomic->val, val);
}

/** Decrement the value of an atomic variable */
static ALWAYS_INLINE void atomic_sub(atomic_t *atomic, __atomic_t val)
{
    stdatomic_sub(&atomic->val, val);
}

/** Increment the value of an atomic variable by one */
static ALWAYS_INLINE void atomic_inc(atomic_t *atomic)
{
    atomic_add(atomic, 1);
}

/** Decrement the value of an atomic variable by one */
static ALWAYS_INLINE void atomic_dec(atomic_t *atomic)
{
    atomic_sub(atomic, 1);
}

#endif /* KERNEL_ATOMIC_H */
