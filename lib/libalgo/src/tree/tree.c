#include <libalgo/queue.h>
#include <libalgo/tree.h>

void tree_add_child(tree_node_t *node, tree_node_t *child)
{
    llist_add_tail(&node->children, &child->this);
    child->parent = node;
}

void tree_add_child_sorted(tree_node_t *node, tree_node_t *child,
                           compare_t compare)
{
    llist_insert_sorted(&node->children, &child->this, compare);
    child->parent = node;
}

tree_node_t *tree_remove(tree_node_t *node)
{
    if (node->parent == NULL)
        return node;

    llist_remove(&node->parent->children, &node->this);
    node->parent = NULL;

    return node;
}

tree_node_t *
tree_find_child(tree_node_t *node, compare_t compare, const void *data)
{
    FOREACH_CHILDREN (child, node) {
        if (!compare(child, data))
            return tree_node(child);
    }

    return NULL;
}

void tree_free(tree_t root, void (*free_function)(tree_node_t *))
{
    DECLARE_QUEUE(nodes);

    queue_enqueue(&nodes, &root->this);

    while (!queue_is_empty(&nodes)) {
        tree_node_t *node = tree_node(queue_dequeue(&nodes));
        queue_enqueue_all(&nodes, node->children);
        free_function(node);
    }
}
