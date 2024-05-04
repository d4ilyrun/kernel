#include <libalgo/avl.h>

static inline int avl_balance_factor(const avl_t *avl)
{
    // Can it ever be null here? I guess we'll have to find out ...
    return avl_height(avl->right) - avl_height(avl->left);
}

static inline void avl_recompute_height(avl_t *avl)
{
    avl->height = 1 + MAX(avl_height(avl->left), avl_height(avl->right));
}

/**
 * @brief Search a node inside an AVL tree and return its address.
 *
 * Searching the location of a node inside the AVL tree (not the node but the
 * address of it as a child) is a common pattern inside this library.
 *
 * @root [in] The adress of the AVL's root
 * @parent [out] A pointer in which to output the child's parent's
 *
 * @return The address of the node's parent's pointer towards it.
 */
static avl_t **avl_search_node(avl_t **root, avl_t *value,
                               avl_compare_t compare, avl_t **parent)
{
    *parent = NULL;

    while (*root != NULL) {
        int res = compare(value, *root);
        if (res == 0)
            return root;

        *parent = *root;

        if (res <= 0)
            root = &(*root)->left;
        else
            root = &(*root)->right;
    }

    return root;
}

const avl_t *avl_search(avl_t *root, avl_t *value, avl_compare_t compare)
{
    avl_t *parent;
    return *avl_search_node(&root, value, compare, &parent);
}

/**
 * @brief Perform a simple rotation (left or right)
 *
 * @node The node around which we want to to rotate
 * @return The new node moved in place of 'node'
 *
 * @warning This function modifies the structure of the tree. DO NOT forget
 *          to retrieve and USE the value it returns.
 *
 * References:
 * * https://en.wikipedia.org/wiki/AVL_tree#Simple_rotation
 */
NO_DISCARD static avl_t *avl_rotate_simple(avl_t *node)
{
    if (node == NULL)
        return node;

    const int bf = avl_balance_factor(node);
    if (bf == 0)
        return node;

    // left rotation if right-heavy, else right rotation
    // note: new_node cannot be null since the balance factor is non-null
    avl_t *new_node = (bf > 0) ? node->right : node->left;

    if (node->parent) {
        // NOTE: Should we keep an avl_t ** to the parent's pointer towards this
        //       node instead? This would avoid this double chek each time,
        //       and we could compute the parent's address using container_of.
        if (node->parent->left == node)
            node->parent->left = new_node;
        else
            node->parent->right = new_node;
    }

    // "switch" X and Z to re-balance the tree
    if (bf > 0) {
        node->right = new_node->left;
        new_node->left = node;
        if (node->right)
            node->right->parent = node;

    } else {
        node->left = new_node->right;
        new_node->right = node;
        if (node->left)
            node->left->parent = node;
    }

    // invert the backlinks to the parents
    new_node->parent = node->parent;
    node->parent = new_node;

    // Update heights
    avl_recompute_height(node);
    avl_recompute_height(new_node);

    return new_node;
}

/**
 * @brief Perform a Righ-Left rotation
 *
 * References:
 * * https://en.wikipedia.org/wiki/AVL_tree#Double_rotation
 */
NO_DISCARD static avl_t *avl_rotate_double_rl(avl_t *node)
{
    if (node == NULL)
        return NULL;

    int bf = avl_balance_factor(node);
    if (bf == 0)
        return node;

    // TODO: I'm sure I could do a cleaner implementation using avl_t **
    //       but fuck it I just want it to work for now

    avl_t *new_root = node->right->left;

    node->right->left = new_root->right;
    if (new_root->right) {
        new_root->right->parent = node->right;
    }

    new_root->right = node->right;
    new_root->right->parent = new_root;

    node->right = new_root->left;
    if (node->right)
        node->right->parent = node;

    new_root->left = node;
    new_root->parent = node->parent;

    // Update the parent's link to the new node
    if (node->parent) {
        // NOTE: As said in avl_rotate_simple, we should consider using avl_t **
        //       for the link to the parent
        if (node->parent->left == node)
            node->parent->left = new_root;
        else
            node->parent->right = new_root;
    }

    node->parent = new_root;

    // Update heights
    // NOTE: Should we keep track of the balance factor instead?
    avl_recompute_height(new_root->right);
    avl_recompute_height(new_root->left);
    avl_recompute_height(new_root);

    return new_root;
}

/**
 * @brief Perform a Left-Right rotation
 *
 * References:
 * * https://en.wikipedia.org/wiki/AVL_tree#Double_rotation
 */
NO_DISCARD static avl_t *avl_rotate_double_lr(avl_t *node)
{
    if (node == NULL)
        return NULL;

    int bf = avl_balance_factor(node);
    if (bf == 0)
        return node;

    // TODO: I'm sure I could do a cleaner implementation using avl_t **
    //       but fuck it I just want it to work for now

    avl_t *new_root = node->left->right;

    node->left->right = new_root->left;
    if (new_root->left) {
        new_root->left->parent = node->left;
    }

    new_root->left = node->left;
    new_root->left->parent = new_root;

    node->left = new_root->right;
    if (node->left)
        node->left->parent = node;

    new_root->right = node;
    new_root->parent = node->parent;

    // Update the parent's link to the new node
    if (node->parent) {
        // NOTE: As said in avl_rotate_simple, we should consider using avl_t **
        //       for the link to the parent
        if (node->parent->right == node)
            node->parent->right = new_root;
        else
            node->parent->left = new_root;
    }

    node->parent = new_root;

    // Update heights
    // NOTE: Should we keep track of the balance factor instead?
    avl_recompute_height(new_root->left);
    avl_recompute_height(new_root->right);
    avl_recompute_height(new_root);

    return new_root;
}

/**
 * Retrace an AVL tree (from end to start), performing the necessary rotations
 * to correct any eventual imbalances.
 *
 * Call this function after modifying the tree (inserting or reomving a node),
 * to ensure its integrity.
 *
 * @return The root of the tree after the rotations
 */
NO_DISCARD static avl_t *avl_retrace_tree(avl_t *leaf)
{
    avl_t *root = leaf;

    for (avl_t *current = leaf; current != NULL;
         root = current, current = current->parent) {

        avl_recompute_height(current);

        int bf = avl_balance_factor(current);

        if (bf >= 2) {
            if (avl_balance_factor(current->right) >= 0)
                current = avl_rotate_simple(current); // LeftLeft
            else
                current = avl_rotate_double_rl(current); // RightLeft
        } else if (bf <= -2) {
            if (avl_balance_factor(current->left) <= 0)
                current = avl_rotate_simple(current); // RightRight
            else
                current = avl_rotate_double_lr(current); // LeftRight
        }
    }

    return root;
}

avl_t *avl_insert(avl_t *root, avl_t *new, avl_compare_t compare)
{
    // TODO: Similar to linux's ERR_PTR
    //       Return a negative pointer containing an error code
    if (new == NULL || new->height > 0 || new->left || new->right ||
        new->parent)
        return NULL; // -EINVAL

    if (root == NULL)
        return new;

    avl_t *parent = NULL;
    avl_t **node = &root;

    // Look for the leaf in wich to insert the new node
    while (*node != NULL) {
        int res = compare(new, *node);
        parent = *node;
        if (res <= 0)
            node = &(*node)->left;
        else
            node = &(*node)->right;
    }

    // Insert the new node inside the leaf
    *node = new;
    (*node)->parent = parent;

    // Retrace the tree to correct eventual imbalances
    // Update root during unwinding in case it changes due to rotation
    return avl_retrace_tree(new);
}

avl_t *avl_remove(avl_t *root, avl_t *value, avl_compare_t compare,
                  bool *removed)
{
    if (root == NULL)
        return NULL;

    if (removed != NULL)
        *removed = true;

    avl_t *parent;
    avl_t **remove = avl_search_node(&root, value, compare, &parent);

    // No equivalent value is present inside the tree
    if (*remove == NULL) {
        if (removed != NULL)
            *removed = false;
        return root;
    }

    // Find the highest lower child if posisble,
    // Else copy the right child (it cannot have children since it is balanced)

    // Starting point of the retracing after updating
    // We want to start retracing at the parent of the node used as replacement
    avl_t **retrace;
    avl_t **replace;

    if ((*remove)->left == NULL) {
        *remove = (*remove)->right;
        retrace = &parent;
    } else {
        retrace = remove;
        replace = &(*remove)->left;
        while ((*replace)->right != NULL) {
            retrace = replace; // start at the parent of the replacement value
            replace = &(*replace)->right;
        }

        // replace highest lower with its (maybe present) lower child
        avl_t *tmp = *replace;
        *replace = (*replace)->left;
        if (*replace != NULL)
            (*replace)->parent = tmp->parent;

        *tmp = **remove; // copy children (+parent but done eventually)
        *remove = tmp;
    }

    // If we did not straight up remove a leaf, we need to update the link back
    // to its new parent
    if (*remove != NULL) {
        (*remove)->parent = parent;
        if ((*remove)->left)
            (*remove)->left->parent = *remove;
        if ((*remove)->right)
            (*remove)->right->parent = *remove;
    }

    // Trace back along the tree to correct eventual imbalances
    // If trace is null, we removed the root
    if (*retrace != NULL)
        return avl_retrace_tree(*retrace);

    return *remove;
}

// NOLINTNEXTLINE(misc-no-recursion)
void avl_print(avl_t *root, void (*print)(const avl_t *))
{
    if (root == NULL)
        return;

    // in-order DF print
    avl_print(root->left, print);
    print(root);
    avl_print(root->right, print);
}

const avl_t *avl_min(const avl_t *root)
{
    if (root == NULL)
        return NULL;

    // the minimum value in an AVL is always the right-most leaf
    while (root->left != NULL)
        root = root->left;

    return root;
}

const avl_t *avl_max(const avl_t *root)
{
    if (root == NULL)
        return NULL;

    // the maximum value in an AVL is always the right-most leaf
    while (root->right != NULL)
        root = root->right;

    return root;
}
