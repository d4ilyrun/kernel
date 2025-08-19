#pragma once

/**
 * @file libalgo/queue.h
 *
 * @defgroup libalgo_queue Queue
 * @ingroup libalgo
 *
 * @{
 */

#include <stdbool.h>

#include "linked_list.h"

/** A queue instance.
 *  Our queue is just a wrapper around a @ref libalgo_linked_list
 */
typedef llist_t queue_t;

/** Default value for queue (empty) */
#define __QUEUE_INIT __LLIST_INIT
#define QUEUE_INIT LLIST_INIT

/** Initialize an empty queue */
#define __INIT_QUEUE __INIT_LLIST
#define INIT_QUEUE INIT_LLIST

/** Declare an empty queue */
#define DECLARE_QUEUE DECLARE_LLIST

/** @return Whether the queue is empty or not */
static inline bool queue_is_empty(const queue_t *queue)
{
    return llist_is_empty(queue);
}

/** Insert the new element as the tail of the queue */
static inline void queue_enqueue(queue_t *queue, node_t *new)
{
    llist_add_after(llist_last(queue), new);
}

/** Pop the head from the queue
 *  @return The popped head, or NULL if empty
 */
static inline node_t *queue_dequeue(queue_t *queue)
{
    return llist_pop(queue);
}

/** @return the current head of the queue */
static inline const node_t *queue_peek(const queue_t *queue)
{
    return llist_first(queue);
}

/** Insert all the elements of a list into a queue.
 *
 *  After this operation, @c elements is empty.
 */
static inline void queue_enqueue_all(queue_t *queue, llist_t *elements)
{
    if (llist_is_empty(elements))
        return;

    llist_first(elements)->prev = llist_last(queue);
    llist_last(queue)->next = llist_first(elements);

    llist_last(elements)->next = llist_head(queue);
    llist_head(queue)->prev = llist_last(elements);

    INIT_LLIST(*elements);
}
