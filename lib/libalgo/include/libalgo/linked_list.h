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

#include <utils/container_of.h>
#include <utils/compiler.h>

/**
 *  Intrusive doubly-linked list node.
 *  @struct linked_list_node
 */
typedef struct linked_list_node {
    struct linked_list_node *next; ///< Next item in the list
    struct linked_list_node *prev; ///< Previous item in the list
} node_t;

/**
 * The head of a doubly linked list.
 *
 * The head does not represent any real entry, it simply points to the first
 * and last entry inside the list. If a list is empty the head points to itself.
 *
 * We use a wrapper struct for the head to avoid wrongfully passing a list
 * entry when the list's head is expected instead.
 *
 * @struct linked_list_head
 */
typedef struct linked_list_head {
    struct linked_list_node head; /** The list's head */
} llist_t;

/** Linked list head default init value (one entry) */
#define __LLIST_INIT(_head) \
    {                       \
        {                   \
            _head, _head    \
        }                   \
    }
#define LLIST_INIT(_list) ((llist_t)__LLIST_INIT(&(_list).head))

/** Initialize an empty linked list head */
#define __INIT_LLIST(_name) _name = __LLIST_INIT(_name)
#define INIT_LLIST(_name) _name = LLIST_INIT(_name)

/** Declare empty linked list head */
#define DECLARE_LLIST(_name) llist_t INIT_LLIST(_name)

/** Declare an intrusive list node. Should be put inside a struct definition. */
#define LLIST_NODE(_name) node_t _name

/** Loop over each element inside a linked list
 *
 *  @param _name The name of the current node
 *  @param _list The head of the linked list
 */
#define FOREACH_LLIST(_name, _list)                                      \
    for (node_t *_name = llist_first(_list); _name != llist_head(_list); \
         _name = llist_next(_name))

/** Loop over each element inside a linked list in a safer way
 *
 *  The next element in the list is stored each time, letting us freely release
 *  the current one without having to dereference it in the next iteration.
 *
 *  @param _name The name of the current node
 *  @param _tmp The name of the node used to store the next pointer
 *  @param _list The head of the list
 */
#define FOREACH_LLIST_SAFE(_name, _tmp, _list)                          \
    for (node_t *_name = llist_first(_list), *_tmp = llist_next(_name); \
         _name != llist_head(_list); _name = _tmp, _tmp = llist_next(_tmp))

/** Loop over each element inside a linked list in reverse order
 *
 *  @param _name The name of the current node
 *  @param _list The tail of the linked list
 */
#define FOREACH_REVERSE_LLIST(_name, _list)                             \
    for (node_t *_name = llist_last(_list); _name != llist_head(_list); \
         _name = llist_prev(_name))

/** Loop over each element inside a linked list in reverse order in asafer way
 *
 *  The prev element in the list is stored each time, letting us freely release
 *  the current one without having to dereference it in the next iteration.
 *
 *  @param _name The name of the current node
 *  @param _tmp The name of the node used to store the prev pointer
 *  @param _list The tail of the linked list
 */
#define FOREACH_REVERSE_LLIST_SAFE(_name, _tmp, _list)                 \
    for (node_t *_name = llist_last(_list), *_tmp = llist_prev(_name); \
         _name != llist_head(_list); _name = _tmp, _tmp = llist_prev(_tmp))

/** Loop over each entry inside a linked list
 *
 *  @param _entry The variable used to store the entry
 *  @param _list  The head of the linked list
 *  @param _field The name of the node field inside the entry's structure
 */
#define FOREACH_LLIST_ENTRY(_entry, _list, _field)                          \
    for (_entry = llist_entry(llist_first(_list), typeof(*_entry), _field); \
         &_entry->_field != llist_head(_list);                              \
         _entry = llist_entry(llist_next(&_entry->_field), typeof(*_entry), \
                              _field))

/** Loop over each entry inside a linked list in a safer manner
 *
 *  @param _entry The variable used to store the entry
 *  @param _tmp   The variable used to store the temporary entry
 *  @param _list  The head of the linked list
 *  @param _field The name of the node field inside the entry's structure
 */
#define FOREACH_LLIST_ENTRY_SAFE(_entry, _tmp, _list, _field)                 \
    for (_entry = llist_entry(llist_first(_list), typeof(*_entry), _field),   \
        _tmp =                                                                \
             llist_entry(llist_next(&_entry->_field), typeof(*_tmp), _field); \
         &_entry->_field != llist_head(_list);                                \
         _entry = _tmp, _tmp = llist_entry(llist_next(&_tmp->_field),         \
                                           typeof(*_entry), _field))

/** Loop over each entry inside a linked list in reverse order
 *
 *  @param _entry The variable used to store the entry
 *  @param _list  The head of the linked list
 *  @param _field The name of the node field inside the entry's structure
 */
#define FOREACH_LLIST_ENTRY_REVERSE(_entry, _list, _field)                  \
    for (_entry = llist_entry(llist_last(_list), typeof(*_entry), _field);  \
         &_entry->_field != llist_head(_list);                              \
         _entry = llist_entry(llist_prev(&_entry->_field), typeof(*_entry), \
                              _field))

/** Loop over each entry inside a linked list in reverse order in a safer manner
 *
 *  @param _entry The variable used to store the entry
 *  @param _tmp   The variable used to store the temporary entry
 *  @param _list  The head of the linked list
 *  @param _field The name of the node field inside the entry's structure
 */
#define FOREACH_LLIST_ENTRY_REVERSE_SAFE(_entry, _tmp, _list, _field)         \
    for (_entry = llist_entry(llist_last(_list), typeof(*_entry), _field),    \
        _tmp =                                                                \
             llist_entry(llist_prev(&_entry->_field), typeof(*_tmp), _field); \
         &_entry->_field != llist_head(_list);                                \
         _entry = _tmp, _tmp = llist_entry(llist_prev(&_tmp->_field),         \
                                           typeof(*_entry), _field))

/** @return the given list's head */
#define llist_head(_list) (&(_list)->head)

/** @return the given list's first entry */
static inline node_t *llist_first(const llist_t *list)
{
    return llist_head(list)->next;
}

/** @return the given list's last entry */
static inline node_t *llist_last(const llist_t *list)
{
    return llist_head(list)->prev;
}

/***/
static inline node_t *llist_next(const node_t *entry)
{
    return entry->next;
}

/***/
static inline node_t *llist_prev(const node_t *entry)
{
    return entry->prev;
}

/**
 * Return the struct containing this list node.
 */
#define llist_entry(ptr, type, member) container_of(ptr, type, member)

/** @return Whether a list is empty */
static PURE inline bool llist_is_empty(const llist_t *list)
{
    return llist_first(list) == llist_head(list);
}

/** Insert a new entry between two known consecutive ones (internal use only) */
static inline void __llist_add(node_t *new, node_t *prev, node_t *next)
{
    next->prev = new;
    new->next = next;
    new->prev = prev;
    prev->next = new;
}

/** Delete a list entry (internal use only).
 *
 *  @param prev The node before the deleted entry
 *  @param next The node after the deleted entry
 */
static inline void __list_remove(node_t *prev, node_t *next)
{
    next->prev = prev;
    prev->next = next;
}

/** Insert a new entry into a list
 *
 * @param prev list entry to add it after
 * @param new list entry to be added
 */
static inline void llist_add_after(node_t *prev, node_t *new)
{
    __llist_add(new, prev, prev->next);
}

/** Insert a new entry into a list
 *
 * @param next list entry to add it before
 * @param new list entry to be added
 */
static inline void llist_add_before(node_t *next, node_t *new)
{
    __llist_add(new, next->prev, next);
}

/** Insert a new entry as the first element of the list
 *
 * @param list list to add into
 * @param new list entry to be added
 */
static inline void llist_add(llist_t *list, node_t *new)
{
    llist_add_after(llist_head(list), new);
}

/** Insert a new entry as the last element of the list
 *
 * @param list list to add into
 * @param new list entry to be added
 */
static inline void llist_add_tail(llist_t *list, node_t *new)
{
    llist_add_after(llist_last(list), new);
}

/** Remove an entry from a list
 *
 *  @param node The entry to remove
 *  @return The removed entry
 */
static inline node_t *llist_remove(node_t *node)
{
    __list_remove(node->prev, node->next);
    return node;
}

/** Pop the first element in a list.
 *
 * @param list A linked list's sentinel
 * @return The list's previous first node, or NULL if empty
 */
static inline node_t *llist_pop(llist_t *list)
{
    if (llist_is_empty(list))
        return NULL;

    return llist_remove(llist_first(list));
}

/** Pop the last element in a list.
 *
 * @param list A linked list's sentinel
 * @return The list's previous tail, or NULL if empty
 */
static inline node_t *llist_pop_tail(llist_t *list)
{
    if (llist_is_empty(list))
        return NULL;

    return llist_remove(llist_last(list));
}

/** Insert a new item inside a sorted list in ascending order
 *
 *  @param list The list's head
 *  @param new The node to be inserted
 *  @param compare The comparison function used to keep the list sorted
 *                 (new is passed as its first argument when called)
 */
static inline void
llist_insert_sorted(llist_t *list, node_t *new, compare_t compare)
{
    node_t *node = llist_first(list);

    while (node != llist_head(list)) {
        if (compare(new, node) <= 0)
            break;
        node = llist_next(node);
    }

    llist_add_before(node, new);
}

/** Insert a new item inside a sorted list in ascending order
 *
 *  Similar to @ref llist_insert_sorted(), but does not create a duplicate.
 *
 *  @param list The list's head
 *  @param new The node to be inserted
 *  @param compare The comparison function used to keep the list sorted
 *                 (@c new is passed as its first argument)
 *
 *  @return @c true if the element was inserted.
 */
static inline bool
llist_insert_sorted_unique(llist_t *list, node_t *new, compare_t compare)
{
    node_t *node = llist_first(list);
    int ret;

    while (node != llist_head(list)) {
        ret = compare(new, node);
        if (ret == 0)
            return false;
        if (ret < 0)
            break;
        node = llist_next(node);
    }

    llist_add_before(node, new);

    return true;
}

/** Retreive the first matching element inside the list (or NULL)
 *
 *  @param list The list's head
 *  @param data The data to be matched against
 *  @param compare The function used to compare the nodes and the data
 *                 (@c data is passed as its second argument)
 *
 *  @return The first matching node, or NULL
 */
static inline node_t *
llist_find_first(const llist_t *list, const void *data, compare_t compare)
{
    FOREACH_LLIST (node, list) {
        if (!compare(node, data))
            return node;
    }

    return NULL;
}
