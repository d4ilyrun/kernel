/**
 * @defgroup libalgo_tree Tree
 * @ingroup libalgo
 *
 * # Tree
 *
 * This is our implementation of an intrusive generic tree structure.
 *
 * @{
 */

#pragma once

#include <kernel/types.h>

#include <libalgo/linked_list.h>
#include <utils/compiler.h>

/** @struct tree_node
 *  @brief A single node inside a tree
 */
typedef struct tree_node {
    node_t this; ///< Linked list node used to link children together
    struct tree_node *parent; ///< Parent node
    llist_t children;         ///< Linked list of children of this node
} tree_node_t;

/** The root of a tree structure */
typedef tree_node_t *tree_t;

/** Loop over each children of a tree node
 *  @param _iter The name of the iterator used inside the loop
 *  @param _node The tree node
 */
#define FOREACH_CHILDREN(_iter, _node) FOREACH_LLIST (_iter, (_node)->children)

/** Convert a linked list node to its containing tree node
 *
 * Children of a node are linked together and addressed using a likned list, it
 * is necessary (and trivial) to perform this conversion when iterating over
 * them.
 */
static ALWAYS_INLINE tree_node_t *tree_node(node_t *node)
{
    return (tree_node_t *)node;
}

/** Add the given node as a children of another
 *
 *  @note This function appends the new child to the current list of children.
 *        If you want to keep the children sorted use \ref tree_add_child_sorted
 */
void tree_add_child(tree_node_t *node, tree_node_t *child);

/** Add the given node as a children of another in a sorted manner
 *
 * @warning This function assumes that the node's children are already sorted
 *
 * @param node The parent node
 * @param child The new child
 * @param compare The compare function used on the children
 */
void tree_add_child_sorted(tree_node_t *node, tree_node_t *child, compare_t);

/** Remove a node from its containing tree (if any).
 *  @return The removed node
 */
tree_node_t *tree_remove(tree_node_t *node);

/** Find a specific child of a node
 *
 *  @param node The parent node
 *  @param compare The compare function used to find the node
 *  @param data Data passed to the compare function
 *
 *  @return The child or \c NULL
 */
tree_node_t *tree_find_child(tree_node_t *node, compare_t, const void *data);

/** Free all the elements contained inside a tree
 *
 *  @param root The root of the tree
 *  @param free The function used to free an element
 */
void tree_free(tree_t root, void (*free_function)(tree_node_t *));

/** @} */
