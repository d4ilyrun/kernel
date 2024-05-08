#include <libalgo/avl.h>
#include <utils/map.h>
#include <utils/math.h>

#include <criterion/assert.h>
#include <criterion/criterion.h>
#include <criterion/logging.h>
#include <errno.h>
#include <stdio.h>

/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 */
#define container_of(ptr, type, member)                    \
    ({                                                     \
        const typeof(((type *)0)->member) *__mptr = (ptr); \
        (type *)((char *)__mptr - offsetof(type, member)); \
    })

typedef struct test_data {
    int value;
    avl_t avl;
} test_data;

static int avl_test_compare(const avl_t *left, const avl_t *right)
{
    test_data *a = container_of(left, test_data, avl);
    test_data *b = container_of(right, test_data, avl);

    if (a->value == b->value)
        return 0;

    return (a->value < b->value) ? -1 : 1;
}

__attribute__((unused)) static void avl_test_print(const avl_t *avl)
{
    test_data *data = container_of(avl, test_data, avl);
    for (int i = 0; i < avl->height; ++i)
        putchar('\t');
    printf("%d\n", data->value);
}

static inline void avl_update_height(avl_t *avl)
{
    avl->height = 1 + MAX(avl_height(avl->left), avl_height(avl->right));
}

static inline int avl_balance_factor(const avl_t *avl)
{
    // Can it ever be null here? I guess we'll have to find out ...
    return avl_height(avl->right) - avl_height(avl->left);
}

static void avl_equal(avl_t *got, avl_t *expected, const avl_t *parent)
{
    if (expected == NULL) {
        cr_assert(got == NULL);
        return;
    }

    cr_assert_neq(got, NULL);

    cr_assert_eq(avl_test_compare(got, expected), 0,
                 "avl_test_compare(got, expected) = %d, should be 0",
                 avl_test_compare(got, expected));

    cr_assert_eq(got->height, expected->height);

    cr_assert_eq(
        got->parent == NULL, parent == NULL,
        "Only the top root of a tree can (and must) not have a parent node");

    cr_assert_eq(got->parent, parent,
                 "Parent pointer does not point to the actual parent");

    cr_assert_lt(ABS(avl_balance_factor(got)), 2, "Tree is Unbalanced");

    avl_equal(got->left, expected->left, got);
    avl_equal(got->right, expected->right, got);
}

static inline void cr_assert_avl_eq(avl_t *got, avl_t *expected)
{
    avl_equal(got, expected, NULL);
}

#define AVL(_suffix, _root, _left, _right)                                   \
    ({                                                                       \
        test_data *_left_tmp_##_suffix = (_left);                            \
        test_data *_right_tmp_##_suffix = (_right);                          \
        test_data *_root_tmp_##_suffix = (_root);                            \
        _root_tmp_##_suffix->avl.left = &_left_tmp_##_suffix->avl;           \
        _root_tmp_##_suffix->avl.right = &_right_tmp_##_suffix->avl;         \
        _left_tmp_##_suffix->avl.parent = _right_tmp_##_suffix->avl.parent = \
            &_root_tmp_##_suffix->avl;                                       \
        avl_update_height(&_root_tmp_##_suffix->avl);                        \
        _root_tmp_##_suffix;                                                 \
    })

// Easily generate the necesssary data for the test
#define DATA(_n) __attribute__((unused)) test_data data##_n = {.value = (_n)};
#define GOT(_n) __attribute__((unused)) test_data got##_n = {.value = (_n)};

#define SETUP(...)          \
    MAP(DATA, __VA_ARGS__); \
    MAP(GOT, __VA_ARGS__);

#define TEST_SEARCH(_n)                                                   \
    cr_assert_eq(avl_search(&root->avl, &data##_n.avl, avl_test_compare), \
                 &data##_n.avl);

Test(Search, Simple)
{
    MAP(DATA, 0, 1, 2, 3, 4, 5, 6);

    test_data *root = AVL(root, &data3, AVL(left, &data1, &data0, &data2),
                          AVL(right, &data5, &data4, &data6));

    MAP(TEST_SEARCH, 0, 1, 2, 3, 4, 5, 6);
}

Test(Search, DuplicateValues)
{
    MAP(DATA, 2, 3);
    test_data data2_bis = data2;

    /*     2
     *    / \
     *   2   3
     */
    test_data *root = AVL(root, &data2, &data2_bis, &data3);

    cr_assert_eq(avl_search(&root->avl, &data2_bis.avl, avl_test_compare),
                 &data2.avl,
                 "Search function should return the FIRST match only");
}

#define TEST_INSERT(_data, _root, _new_root)                              \
    {                                                                     \
        avl_t *_inserted = avl_insert(&_root, (_data), avl_test_compare); \
        cr_assert_avl_eq(_root, (_new_root));                             \
        cr_assert_eq(_inserted, _data);                                   \
    }

Test(Insert, Simple)
{
    // Simple straight-forward ordered insert with no rotation needed

    SETUP(0, 1, 2);

    test_data *expected = AVL(0, &data1, &data0, &data2);

    avl_t *got = &got1.avl;

    // 0 <- 1
    TEST_INSERT(&got0.avl, got, &got1.avl);
    // 0 <- 1 -> 2
    TEST_INSERT(&got2.avl, got, &got1.avl);

    cr_assert_avl_eq(got, &expected->avl);
}

Test(Insert, EmptyTree)
{
    SETUP(0);

    avl_t *got = NULL;

    TEST_INSERT(&got0.avl, got, &got0.avl);

    cr_assert_null(got->parent);
    cr_assert_null(got->left);
    cr_assert_null(got->right);
    cr_assert_eq(got->height, 0);
}

Test(Insert, NonEmpty)
{
    MAP(DATA, 0, 1, 2, 3, 4);

    data1.avl.parent = &data0.avl;
    data2.avl.right = &data0.avl;
    data3.avl.left = &data0.avl;
    data4.avl.left = &data0.avl;

    avl_t *root = &data0.avl;

    cr_assert_null(avl_insert(&root, &data1.avl, avl_test_compare));
    cr_assert_null(avl_insert(&root, &data2.avl, avl_test_compare));
    cr_assert_null(avl_insert(&root, &data3.avl, avl_test_compare));
    cr_assert_null(avl_insert(&root, &data4.avl, avl_test_compare));
}

Test(Insert, Rotation_Left)
{
    SETUP(1, 2, 3, 4, 5);

    // Need to perform a left rotation on the right child to re-balance this
    // tree, or else it would look like 1 <- 2 -> 3 -> 4 -> 5 (bf = 2)

    test_data *expected =
        AVL(root, &data2, &data1, AVL(right, &data4, &data3, &data5));

    avl_t *got = &got2.avl;

    TEST_INSERT(&got1.avl, got, &got2.avl);
    TEST_INSERT(&got3.avl, got, &got2.avl);
    TEST_INSERT(&got4.avl, got, &got2.avl);
    TEST_INSERT(&got5.avl, got, &got2.avl);

    cr_assert_avl_eq(got, &expected->avl);
}

Test(Insert, Rotation_Right)
{
    SETUP(1, 2, 3, 4, 5);

    // Need to perform a left rotation on the right child to re-balance this
    // tree, or else it would look like 1 <- 2 <- 3 <- 4 -> 5 (bf = -2)

    test_data *expected =
        AVL(root, &data4, AVL(left, &data2, &data1, &data3), &data5);

    avl_t *got = &got4.avl;

    TEST_INSERT(&got5.avl, got, &got4.avl);
    TEST_INSERT(&got3.avl, got, &got4.avl);
    TEST_INSERT(&got2.avl, got, &got4.avl);
    TEST_INSERT(&got1.avl, got, &got4.avl);

    cr_assert_avl_eq(got, &expected->avl);
}

Test(Insert, Rotation_NewRoot)
{
    SETUP(1, 2, 3);

    test_data *expected = AVL(, &data2, &data1, &data3);

    avl_t *got = &got3.avl;

    TEST_INSERT(&got2.avl, got, &got3.avl);
    TEST_INSERT(&got1.avl, got, &got2.avl);

    cr_assert_avl_eq(got, &expected->avl);
}

Test(Insert, Rotation_LeftRight)
{
    SETUP(2, 4, 6, 7, 8, 9);

    test_data *expected =
        AVL(root, &data7, AVL(left, &data4, &data2, &data6), &data8);

    // We can only build complete trees with the AVL macro ...
    data8.avl.right = &data9.avl;
    data8.avl.height += 1;
    data9.avl.parent = &data8.avl;

    avl_t *got = &got8.avl;

    TEST_INSERT(&got9.avl, got, &got8.avl);
    TEST_INSERT(&got4.avl, got, &got8.avl);
    TEST_INSERT(&got2.avl, got, &got8.avl);
    TEST_INSERT(&got7.avl, got, &got8.avl);
    TEST_INSERT(&got6.avl, got, &got7.avl);

    cr_assert_avl_eq(got, &expected->avl);
}

Test(Insert, Rotation_RightLeft)
{
    SETUP(2, 4, 6, 7, 8, 9);

    test_data *expected =
        AVL(root, &data6, &data4, AVL(right, &data8, &data7, &data9));

    // We can only build complete trees with the AVL macro ...
    data4.avl.left = &data2.avl;
    data4.avl.height += 1;
    data2.avl.parent = &data4.avl;

    avl_t *got = &got4.avl;

    TEST_INSERT(&got2.avl, got, &got4.avl);
    TEST_INSERT(&got8.avl, got, &got4.avl);
    TEST_INSERT(&got9.avl, got, &got4.avl);
    TEST_INSERT(&got6.avl, got, &got4.avl);
    TEST_INSERT(&got7.avl, got, &got6.avl);

    cr_assert_avl_eq(got, &expected->avl);
}

#define TEST_REMOVE1(_data, _root, _new_root, _removed)                 \
    {                                                                   \
        avl_t *removed = avl_remove(&_root, (_data), avl_test_compare); \
        cr_assert_eq(_root, (_new_root));                               \
        cr_assert_eq(removed, _removed);                                \
    }

#define TEST_REMOVE(_data, _root, _new_root) \
    TEST_REMOVE1(_data, _root, _new_root, _data);

Test(Remove, Leaf)
{
    SETUP(1, 2, 3, 4);

    test_data *expected = AVL(root, &data2, &data1, &data3);

    // Can only create complete trees using this macro ...
    // Manually insert a leaf at the far right of the tree
    test_data *start = AVL(root, &got2, &got1, &got3);
    start->avl.right->right = &got4.avl;
    got4.avl.parent = start->avl.right;
    start->avl.right->height += 1;
    start->avl.height += 1;

    avl_t *got = &start->avl;

    TEST_REMOVE(&got4.avl, got, &start->avl);

    cr_assert_avl_eq(got, &expected->avl);
}

Test(Remove, NotFound)
{
    SETUP(1, 2, 3, 4);

    test_data *expected = AVL(root, &got2, &got1, &got3);

    test_data *start = AVL(root, &got2, &got1, &got3);
    avl_t *got = &start->avl;

    // 4 is not inside the tree, &removed should be set to false
    TEST_REMOVE1(&got4.avl, got, &got2.avl, NULL);

    cr_assert_avl_eq(got, &expected->avl);
}

Test(Remove, Empty)
{
    SETUP(0);

    test_data *start = &got0;
    avl_t *got = &start->avl;

    TEST_REMOVE(&got0.avl, got, NULL);
}

Test(Remove, Root)
{
    SETUP(0, 1, 2, 3, 4);

    test_data *expected = AVL(root, &data2, &data1, &data4);

    expected->avl.left->left = &data0.avl;
    data0.avl.parent = expected->avl.left;
    expected->avl.left->height += 1;
    expected->avl.height += 1;

    test_data *start = AVL(root, &got3, AVL(left, &got1, &got0, &got2), &got4);

    avl_t *got = &start->avl;

    TEST_REMOVE(&got3.avl, got, &got2.avl);

    cr_assert_avl_eq(got, &expected->avl);
}

Test(Remove, Regular)
{
    SETUP(0, 1, 2, 3);

    test_data *expected = AVL(, &data2, &data0, &data3);

    test_data *start = AVL(, &got2, &got1, &got3);

    start->avl.left->left = &got0.avl;
    got0.avl.parent = start->avl.left;
    start->avl.left->height += 1;
    start->avl.height += 1;

    avl_t *got = &start->avl;

    TEST_REMOVE(&got1.avl, got, &start->avl);

    cr_assert_avl_eq(got, &expected->avl);
}

Test(Remove, NoLeftChild)
{
    SETUP(0, 1, 2, 3);

    test_data *expected = AVL(, &data2, &data1, &data3);

    test_data *start = AVL(, &got2, &got0, &got3);

    start->avl.left->right = &got1.avl;
    got1.avl.parent = start->avl.left;
    start->avl.left->height += 1;
    start->avl.height += 1;

    avl_t *got = &start->avl;

    TEST_REMOVE(&got0.avl, got, &start->avl);

    cr_assert_avl_eq(got, &expected->avl);
}

Test(Remove, NoLeftNoParent)
{
    SETUP(0, 1);

    avl_t *expected = &data1.avl;

    data0.avl.right = &data1.avl;
    data1.avl.parent = &data0.avl;
    data0.avl.height = 1;

    avl_t *got = &data0.avl;

    TEST_REMOVE(&data0.avl, got, &data1.avl);

    cr_assert_avl_eq(got, expected);
}

Test(Remove, MultipleRotations)
{
    SETUP(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);

    // clang-format off


    /*
     * The starting tree looks like this:
     *
     *        4
     *      /  \
     *    /      \
     *   1        9
     *  / \      /  \
     * 0  2     6    11
     *     \   / \   / \
     *      3 5  7  10  12
     *            \
     *             8
     *
     * By removing 0, we must rebalance left rotation around 1, which causes the root to become right heavy.
     * We should then perform a RightLeft rotation around 4 to rebalance the whole tree.
     */

    test_data *start =
        AVL(root, &got4,
            AVL(left, &got1, &got0, &got2),
            AVL(right, &got9,
                AVL(right_left, &got6, &got5, &got7),
                AVL(right_right, &got11, &got10, &got12)
            )
        );

    got7.avl.right = &got8.avl;
    got8.avl.parent = &got7.avl;
    got7.avl.height += 1;
    got6.avl.height += 1;
    got9.avl.height += 1;
    got4.avl.height += 1;

    got2.avl.right = &got3.avl;
    got3.avl.parent = &got2.avl;
    got2.avl.height += 1;
    got1.avl.height += 1;

    test_data *expected =
        AVL(root, &data6,
            AVL(left, &data4,
                AVL(left_left, &data2, &data1, &data3),
                &data5
            ),
            AVL(right, &data9,
                &data7,
                AVL(right_right, &data11, &data10, &data12)
            )
        );

    data7.avl.right = &data8.avl;
    data8.avl.parent = &data7.avl;
    data7.avl.height += 1;

    avl_t *got = &start->avl;

    TEST_REMOVE(&got0.avl, got, &got6.avl);

    cr_assert_avl_eq(got, &expected->avl);
}
