/*
 * @file libalgo/linked_list.h
 *
 * @defgroup libalgo_ringbuffer Ring buffer
 * @ingroup libalgo
 *
 * # Ring buffer
 *
 * @{
 */

#ifndef _LIBALGO_RINGBUFFER_H
#define _LIBALGO_RINGBUFFER_H

#include <stddef.h>
#include <stdint.h>

/** @struct ringbuffer */
struct ringbuffer {
    uint8_t *buf_start;
    uint8_t *buf_end;
    uint8_t *buf_read_pos;
    uint8_t *buf_write_pos;
};

/** Reset a ringbuffer's read and write pointers.
 *
 * Calling @ref ringbuffer_remaining after this returns 0.
 */
static inline void ringbuffer_reset(struct ringbuffer *rb)
{
    rb->buf_read_pos = rb->buf_start;
    rb->buf_write_pos = rb->buf_start;
}

/** Initialize a ringbuffer's pointers. */
static inline void
ringbuffer_init(struct ringbuffer *rb, void *buffer, size_t buffer_size)
{
    rb->buf_start = buffer;
    rb->buf_end = buffer + buffer_size;
    ringbuffer_reset(rb);
}

/**
 * Compute the number of free bytes inside a ringbuffer.
 */
static inline size_t ringbuffer_available(const struct ringbuffer *rb)
{
    if (rb->buf_write_pos >= rb->buf_read_pos)
        return (rb->buf_end - rb->buf_write_pos) +
               (rb->buf_read_pos - rb->buf_start);

    return rb->buf_read_pos - rb->buf_write_pos;
}

/**
 * Compute the number of bytes inside a tinringbuffer.
 */
static inline size_t ringbuffer_remaining(const struct ringbuffer *rb)
{
    return rb->buf_end - rb->buf_start - ringbuffer_available(rb);
}

/**
 * Append data to the end of a ringbuffer.
 */
size_t ringbuffer_push(struct ringbuffer *rb, const uint8_t *data, size_t size);

/**
 * Read and remove data from the beginning of a rb's buffer.
 */
size_t ringbuffer_pop(struct ringbuffer *rb, uint8_t *data, size_t size);

/**
 * Read data from the beginning of a rb's buffer.
 */
size_t ringbuffer_peek(const struct ringbuffer *rb, uint8_t *data, size_t size);

#endif /* _LIBALGO_RINGBUFFER_H */

/* @} */
