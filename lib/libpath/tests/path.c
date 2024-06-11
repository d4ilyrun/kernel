#include <lib/path.h>
#include <utils/macro.h>

#include <criterion/criterion.h>
#include <criterion/parameterized.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct path_test_params {
    const char *path;
    const char **components;
    const size_t count;
};

static void libpath_test(struct path_test_params params)
{
    path_segment_t segment;
    path_t path = {.path = params.path, .len = strlen(params.path)};

    if (params.count == 0) {
        cr_assert_not(path_walk_first(&path, &segment));
        cr_assert_not(path_walk_last(&path, &segment));
        return;
    }

    cr_assert(path_walk_first(&path, &segment));
    cr_assert_not(path_walk_prev(&segment),
                  "Should not be able to walk before the first segment");

    for (size_t i = 0; i < params.count; ++i) {
        cr_assert(path_segment_is(params.components[i], &segment));
        if (i == params.count - 1)
            cr_assert_not(path_walk_next(&segment));
        else
            cr_assert(path_walk_next(&segment));
    }

    for (size_t i = params.count; i > 0; --i) {
        cr_assert(path_segment_is(params.components[i - 1], &segment));
        if (i == 1)
            cr_assert_not(path_walk_prev(&segment));
        else
            cr_assert(path_walk_prev(&segment));
    }

    cr_assert(path_walk_last(&path, &segment));
    cr_assert_not(path_walk_next(&segment),
                  "Should not be able to walk past the last segment");

    for (size_t i = params.count; i > 0; --i) {
        cr_assert(path_segment_is(params.components[i - 1], &segment));
        if (i == 1)
            cr_assert_not(path_walk_prev(&segment));
        else
            cr_assert(path_walk_prev(&segment));
    }

    for (size_t i = 0; i < params.count; ++i) {
        cr_assert(path_segment_is(params.components[i], &segment));
        if (i == params.count - 1)
            cr_assert_not(path_walk_next(&segment));
        else
            cr_assert(path_walk_next(&segment));
    }
}

#define PATH_TEST(_test_suite, _test_name, _path, ...) \
    Test(_test_suite, _test_name)                      \
    {                                                  \
        const char *components[] = {__VA_ARGS__};      \
        struct path_test_params params = {             \
            .path = _path,                             \
            .components = components,                  \
            .count = ARRAY_SIZE(components),           \
        };                                             \
                                                       \
        libpath_test(params);                          \
    }

static ssize_t libpath_test_parent(const char *raw_path, const char *parent)
{
    path_t path = NEW_DYNAMIC_PATH(raw_path);
    char *buf = malloc(path.len + 1);

    ssize_t parent_length = path_load_parent(buf, &path, path.len + 1);

    if (parent_length == -1) {
        free(buf);
        return -1;
    }

    cr_assert_str_eq(buf, parent);
    free(buf);

    return parent_length;
}

#define PARENT_TEST(_test_name, _path, _parent, _expected)            \
    Test(Parent, _test_name)                                          \
    {                                                                 \
        ssize_t length = libpath_test_parent(_path, _parent);         \
        cr_assert_eq(length, _expected,                               \
                     "Invalid length: got %ld instead of %d", length, \
                     _expected);                                      \
    }

Test(Path, Empty)
{
    struct path_test_params params = {
        .path = "",
        .components = NULL,
        .count = 0,
    };

    libpath_test(params);
}

Test(Path, Root)
{
    struct path_test_params params = {
        .path = "/",
        .components = NULL,
        .count = 0,
    };

    libpath_test(params);
}

// clang-format off

PATH_TEST(Absolute, SingleComponent, "/etc", "etc")
PATH_TEST(Absolute, MultipleComponents, "/etc/fstab", "etc", "fstab")
PATH_TEST(Absolute, EmptyComponents, "/etc//usr////bin/sh", "etc", "usr", "bin", "sh")
PATH_TEST(Absolute, Directory, "/etc/ssl/", "etc", "ssl")
PATH_TEST(Absolute, DirectoryEmptyComponent, "/etc/ssl///", "etc", "ssl")

PATH_TEST(Relative, SingleComponent, "etc", "etc")
PATH_TEST(Relative, MultipleComponents, "etc/fstab", "etc", "fstab")
PATH_TEST(Relative, EmptyComponents, "etc//usr////bin/sh", "etc", "usr", "bin", "sh")
PATH_TEST(Relative, Directory, "etc/ssl/", "etc", "ssl")
PATH_TEST(Relative, DirectoryEmptyComponent, "etc/ssl///", "etc", "ssl")
PATH_TEST(Relative, SpecialCharacters, "../../../kernel/./main.c", "..", "..", "..", "kernel", ".", "main.c")

PARENT_TEST(Empty, "", "", -1)
PARENT_TEST(Simple, "/usr/bin/toto", "/usr/bin", 8)
PARENT_TEST(Root, "/usr", "/", 1)
PARENT_TEST(RootAbsolute, "usr", "", 0)
PARENT_TEST(MultipleSeparators, "/usr///bin", "/usr", 4)
PARENT_TEST(Directory, "/usr/bin/", "/usr", 4)
