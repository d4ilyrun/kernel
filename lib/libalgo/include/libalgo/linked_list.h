#pragma once

/**
 * @file libalgo/linked_list.h
 *
 * @defgroup libalgo_linked_list Linked List
 * @ingroup libalgo
 *
 * # Linked list
 *
 * This is our implementation for linked lists.
 *
 * @note We use an intrusive linked list design.
 *
 * @{
 */

#include <utils/compiler.h>

#include <stddef.h>

struct linked_list_node {
    struct linked_list_node *next; ///< Next item in the list
};

/**
 * @struct linked_list_node
 * @brief A linked list node
 */
typedef struct linked_list_node llist_node_t;

/** The head of a linked list */
typedef llist_node_t *llist_t;

/** Declare a head to an empty linked list */
#define DECLARE_LLIST(_name) llist_t _name = NULL;

/** Loop over each element inside a linked list
 *  @param _name The name of the current node
 *  @param _head The head of the linked list
 */
#define FOREACH_LLIST(_name, _head) \
    for (llist_node_t *_name = (_head); _name; _name = _name->next)

/** Prepend a new node to the given list */
static ALWAYS_INLINE void llist_add(llist_t *head, llist_node_t *new)
{
    new->next = *head;
    *head = new;
}

/** Append a new node to the given list */
static ALWAYS_INLINE void llist_add_tail(llist_t *head, llist_node_t *new)
{
    while (*head != NULL)
        head = &(*head)->next;

    *head = new;
}

/** Pop the first element of the list */
static ALWAYS_INLINE llist_node_t *llist_pop(llist_t *head)
{
    llist_node_t *old_head = *head;
    *head = (*head)->next;
    return old_head;
}

/** Pop the last element of the list */
static ALWAYS_INLINE llist_node_t *llist_pop_tail(llist_t *head)
{
    llist_node_t *old_tail = *head;

    while (*head != NULL) {
        old_tail = *head;
        head = &(*head)->next;
    }

    if (*head != NULL)
        *head = (*head)->next;

    return old_tail;
}

static ALWAYS_INLINE void llist_remove(llist_t *head, llist_node_t *node)
{
    while (*head != NULL) {
        if (*head == node) {
            *head = node->next;
            return;
        }
        head = &(*head)->next;
    }
}

/** Return the given list's head */
static ALWAYS_INLINE const llist_node_t *llist_head(llist_t head)
{
    return head;
}

/** Return the given list's tail */
static ALWAYS_INLINE const llist_node_t *llist_tail(llist_t head)
{
    if (head == NULL)
        return NULL;

    while (head->next != NULL)
        head = head->next;
    return head;
}
