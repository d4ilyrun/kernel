#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define err(_fmt, ...) fprintf(stderr, _fmt __VA_OPT__(,) __VA_ARGS__)
#define warn(_fmt, ...) fprintf(stderr, _fmt __VA_OPT__(,) __VA_ARGS__)

static int do_ls(const char *path, bool one_per_line)
{
    DIR *dir;
    struct dirent *entry;

    dir = opendir(path);
    if (dir == NULL) {
        err("failed to open %s: %s\n", path, strerror(errno));
        return EXIT_FAILURE;
    }

    while ((entry = readdir(dir)) != NULL)
        printf("%s%s", entry->d_name, one_per_line ? "\n" : " ");

    if (!one_per_line)
        printf("\n");
    printf("\n");

    closedir(dir);

    return 0;
}

static void usage(void)
{
    warn("ls [OPTIONS] DIR [DIR...]\n");
    warn("\n");
    warn("available options:\n");
    warn(" -l   List one entry per line\n");
}

int main(int argc, const char **argv)
{
    bool one_per_line = false;
    int ret;

    /* parse options */
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] != '-') {
            argv = &argv[i];
            argc -= i;
            break;
        }
        if (!strcmp(argv[i], "-h")) {
            usage();
            exit(EXIT_FAILURE);
        }
        if (!strcmp(argv[i], "-l"))
            one_per_line = true;
    }

    /* TODO: getcwd() */
    if (argc < 2)
        return do_ls("", one_per_line);

    for (int i = 0; i < argc; ++i) {
        if (argc > 1)
            printf("%s:\n", argv[i]);
        ret = do_ls(argv[i], one_per_line);
        if (ret)
            return ret;
    }

    return EXIT_SUCCESS;
}
