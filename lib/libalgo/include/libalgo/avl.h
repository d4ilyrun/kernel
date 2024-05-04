/**
 * @brief AVL tree implementation.
 *
 * @file libalgo/avl.h
 * @author Léo DUBOIN <leo@duboin.com>
 *
 * @defgroup AVL AVL tree
 * @ingroup libalgo
 *
 * # AVL
 *
 * An AVL tree is a self-balancing BST where the difference between heights of
 * left and right subtrees for any node cannot be more than one.
 *
 * see: https://en.wikipedia.org/wiki/AVL_tree
 *
 * ## Design
 *
 * This library is designed to be used with intrusive structs.
 * As such, it does NOT allocate nor free any memory whatsoever.
 *
 * This design is highly inspired by linux's intrusive linked lists:
 * https://www.data-structures-in-practice.com/intrusive-linked-lists/
 *
 *
 *  @{
 */

#ifndef LIBALGO_AVL_H
#define LIBALGO_AVL_H

#include <utils/compiler.h>
#include <utils/macro.h>
#include <utils/types.h>

#include <stdbool.h>
#include <stddef.h>

typedef struct avl avl_t;

/**
 * @struct avl
 * @brief A single node of an AVL tree
 *
 * A complete tree is formed of multiple single nodes linked between them.
 *
 * We do not have a struct for the whole tree, as it can simply be
 * addressed using its root node. Keep in mind that, in this implementation,
 * when referring to a "tree" we're instead referring to "the tree that starts
 * at the given node" (i.e. the node and its children).
 */
struct avl {
    avl_t *left;    ///< The left child
    avl_t *right;   ///< The right child
    avl_t *parent;  ///< The parent, NULL if this is the root
    ssize_t height; ///< Height of the tree
};

/** Returns the height of an AVL tree */
static inline ssize_t avl_height(const avl_t *avl)
{
    return (avl == NULL) ? -1 : avl->height;
}

/** Returns hether the given AVL node is the root of a tree */
static inline bool avl_is_root(const avl_t *avl)
{
    return avl->parent == NULL;
}

/** Create an empty AVL node, with no child nor parent */
#define AVL_EMPTY_NODE ((avl_t){0})

/**
 * Comparison function used during AVL tree operations.
 * It performs an arbitrary comparison between 2 AVL nodes.
 * @return 0 if both are equal equal, -1 if left is lower, 1 if its greater.
 */
typedef int (*avl_compare_t)(const avl_t *left, const avl_t *right);

/**
 * @brief Insert a new node inside an AVL tree
 *
 * @root The root of the AVL tree
 * @new The new node to insert inside the tree
 * @compare The comparison function used for this tree
 *
 * @warning Inserting a new node inside an AVL tree may require a rotation in
 *          order to keep it balanced. DO NOT forget to replace the old root of
 *          the tree with the one returned by this function.
 *
 * @return The new root of the modified tree
 */
NO_DISCARD avl_t *avl_insert(avl_t *root, avl_t *new, avl_compare_t);

/**
 * @brief Search a value inside an AVL tree
 *
 * @root The root of the AVL tree
 * @value The value to find
 * @compare The comparison function used for this AVL tree
 *
 * @return The found node if any, else NULL
 *
 * Value must be of the AVL type to be used with the comparison function.
 * But its content need not necessarily be complete, as long as it has the
 * necesary content to perform the comparison operation.
 *
 * @note @c value is passed as the left parameter of @c comparison
 */
const avl_t *avl_search(avl_t *root, avl_t *value, avl_compare_t);

/**
 * @brief Remove a value from an AVL tree
 *
 * @root The root of the AVL tree
 * @value The value to remove from the AVL tree
 * @compare The comparison function used for this tree
 *
 * @remove [out] If not null, store wether a value to remove was found
 *
 * @note @c value is passed as the left parameter of @c comparison
 *
 * @warning Inserting a new node inside an AVL tree may require a rotation in
 *          order to keep it balanced. DO NOT forget to replace the old root of
 *          the tree with the one returned by this function.
 *
 * @return The new root of the modified tree
 */
NO_DISCARD avl_t *avl_remove(avl_t *root, avl_t *value, avl_compare_t,
                             bool *removed);

/**
 * @brief Print the content of an AVL tree
 *
 * The AVL is printed in an in-order depth-first way.
 *
 * @warning This function visit the tree recursively, this means a higher stack
 * usage. Be careful when you use it (more particularily inside the kernel).
 *
 * @root The root of the AVL tree
 * @print The function used to print the content of a node
 */
void avl_print(avl_t *root, void (*print)(const avl_t *));

/**
 * @brief Find the minimum value inside an AVL tree
 * @param root The root of the tree
 * @return The minimum value's node, NULL if tree is empty
 */
const avl_t *avl_min(const avl_t *root);

/**
 * @brief Find the maximum value inside an AVL tree
 * @param root The root of the tree
 * @return The maximum value's node, NULL if tree is empty
 */
const avl_t *avl_max(const avl_t *root);

/** @} */

#endif /* LIBALGO_AVL_H */
