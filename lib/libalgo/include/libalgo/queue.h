#pragma once

/**
 * @file libalgo/queue.h
 *
 * @defgroup libalgo_queue Queue
 * @ingroup libalgo
 *
 * # Queue
 *
 * Our queue is just a wrapper around a @ref llist_t
 *
 * @{
 */

#include <stdbool.h>

#include "linked_list.h"

/** @struct queue
 *  @brief A single queue instance
 */
typedef struct queue {
    node_t *head; ///< The head of the queue, NULL if empty
    node_t *tail; ///< The tail of the queue, NULL if empty
} queue_t;

/** Declare an empty queue */
#define DECLARE_QUEUE(_name) queue_t _name = {NULL, NULL};

/** @return Whether the queue is empty or not */
static ALWAYS_INLINE bool queue_is_empty(const queue_t *queue)
{
    return queue->head == NULL;
}

/** Insert the new element as the tail of the queue */
static inline void queue_enqueue(queue_t *queue, node_t *new)
{
    new->next = NULL;

    if (queue_is_empty(queue))
        queue->head = new;
    else
        __llist_add(&queue->tail->next, queue->tail, new);

    queue->tail = new;
}

/** Remove the head from the queue and return it */
static inline node_t *queue_dequeue(queue_t *queue)
{
    node_t *head = llist_pop(&queue->head);
    if (queue_is_empty(queue))
        queue->tail = NULL;
    return head;
}

/** Return the head of the queue, but do not remove it */
static ALWAYS_INLINE const node_t *queue_peek(const queue_t *queue)
{
    return queue->head;
}

static inline void queue_enqueue_all(queue_t *queue, llist_t elements)
{
    if (llist_is_empty(elements))
        return;

    if (queue_is_empty(queue))
        queue->head = elements;
    else {
        queue->tail->next = elements;
        elements->prev = queue->tail;
    }

    queue->tail = (node_t *)llist_tail(elements);
}
