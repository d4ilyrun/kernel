#include <unistd.h>

extern int main(int argc, char **argv, char **envp);

extern char _edata;
extern char _end;

void _start(int argc, char **argv, char **envp)
{
    char *bss;
    int ret;

    /* Fill bss with zeros */
    bss = &_edata + 1;
    while (bss < &_end)
        *bss++ = 0;

    ret = main(argc, argv, envp);
    _exit(ret);
}
