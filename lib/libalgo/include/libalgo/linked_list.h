#pragma once

/**
 * @file libalgo/linked_list.h
 *
 * @defgroup libalgo_linked_list Linked List
 * @ingroup libalgo
 *
 * # Linked list
 *
 * This is our implementation for doubly linked lists.
 *
 * @note We use an intrusive linked list design.
 *
 * @{
 */

#include <kernel/types.h>

#include <utils/compiler.h>

#include <stddef.h>

struct linked_list_node {
    struct linked_list_node *next; ///< Next item in the list
    struct linked_list_node *prev; ///< Previous item in the list
};

/**
 * @struct linked_list_node
 * @brief A doubly linked list node
 */
typedef struct linked_list_node node_t;

/** The head of a doubly linked list */
typedef node_t *llist_t;

/** Declare a head to an empty linked list */
#define DECLARE_LLIST(_name) llist_t _name = NULL

/** Loop over each element inside a linked list
 *  @param _name The name of the current node
 *  @param _head The head of the linked list
 */
#define FOREACH_LLIST(_name, _head) \
    for (node_t *_name = (_head); _name; _name = _name->next)

/** Loop over each element inside a linked list in reverse order
 *  @param _name The name of the current node
 *  @param _tail The tail of the linked list
 */
#define FOREACH_REVERSE_LLIST(_name, _head) \
    for (node_t *_name = (_tail); _name; _name = _name->prev)

static ALWAYS_INLINE bool llist_is_empty(llist_t list)
{
    return list == NULL;
}

static inline void __llist_add(node_t **node, node_t *prev, node_t *new)
{
    new->next = *node;
    new->prev = prev;

    if (new->next)
        new->next->prev = new;

    *node = new;
}

/** Prepend a new node to the given list */
static ALWAYS_INLINE void llist_add(llist_t *head, node_t *new)
{
    __llist_add(head, NULL, new);
}

/** Append a new node to the given list */
static inline void llist_add_tail(llist_t *head, node_t *new)
{
    while (*head != NULL)
        head = &(*head)->next;

    llist_add(head, new);
}

/** Pop the first element of the list */
static inline node_t *llist_pop(llist_t *head)
{
    node_t *old_head = *head;

    // we assume head = &head->prev->next
    // This is supposed to be the head anyway.
    // We only updated the next->prev to be able to use it in the other helpers
    if (old_head) {
        if (old_head->next) {
            old_head->next->prev = old_head->prev;
            *head = old_head->next;
        }

        *head = old_head->next;
    }

    return old_head;
}

/** Pop the last element of the list */
static inline node_t *llist_pop_tail(llist_t *head)
{
    if (*head == NULL)
        return NULL;

    while ((*head)->next != NULL)
        head = &(*head)->next;

    return llist_pop(head);
}

static inline void llist_remove(llist_t *head, node_t *node)
{
    while (*head != NULL) {
        if (*head == node) {
            llist_pop(head);
            return;
        }
        head = &(*head)->next;
    }
}

/** Return the given list's head */
static ALWAYS_INLINE const node_t *llist_head(llist_t head)
{
    return head;
}

/** Return the given list's tail */
static inline const node_t *llist_tail(llist_t head)
{
    if (head == NULL)
        return NULL;

    while (head->next != NULL)
        head = head->next;
    return head;
}

/** Insert a new item inside a sorted list in asending order */
static inline void
llist_insert_sorted(llist_t *head, node_t *new, compare_t compare)
{
    node_t *prev = NULL;

    while (*head != NULL) {
        if (compare(new, *head) <= 0)
            break;
        prev = *head;
        head = &(*head)->next;
    }

    __llist_add(head, prev, new);
}

/** Retreive the first matching element inside the list (or NULL) */
static inline node_t *
llist_find_first(llist_t head, const void *data, compare_t compare)
{
    for (node_t *node = (head); node; node = node->next) {
        if (!compare(node, data))
            return node;
    }

    return NULL;
}
