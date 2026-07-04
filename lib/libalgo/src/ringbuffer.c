#include <libalgo/ringbuffer.h>

/*
 * Append data to the end of a pipe's buffer.
 */
size_t ringbuffer_push(struct ringbuffer *rb, const uint8_t *data, size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        if (rb->buf_write_pos == rb->buf_end)
            rb->buf_write_pos = rb->buf_start;
        *(rb->buf_write_pos++) = data[i];
    }

    return size;
}

/*
 * Read and remove data from the beginning of a pipe's buffer.
 */
size_t ringbuffer_pop(struct ringbuffer *rb, uint8_t *data, size_t size)
{
    for (size_t i = 0; i < size; ++i) {
        data[i] = *(rb->buf_read_pos++);
        if (rb->buf_read_pos == rb->buf_end)
            rb->buf_read_pos = rb->buf_start;
    }

    return size;
}

/*
 * Read data from the beginning of a pipe's buffer.
 */
size_t ringbuffer_peek(const struct ringbuffer *rb, uint8_t *data, size_t size)
{
    uint8_t *ptr = rb->buf_read_pos;

    for (size_t i = 0; i < size; ++i) {
        data[i] = *(ptr++);
        if (ptr == rb->buf_end)
            ptr = rb->buf_start;
    }

    return size;
}
