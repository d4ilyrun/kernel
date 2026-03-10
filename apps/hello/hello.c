/*
 * Simple hello world program
 *
 * This can be used to debug and test our toolchain.
 */

#include <stdio.h>

int main(int argc, const char **argv)
{
    const char *who = "World";

    if (argc > 1)
        who = argv[1];

    printf("Hello, %s\n", who);

    return 0;
}
