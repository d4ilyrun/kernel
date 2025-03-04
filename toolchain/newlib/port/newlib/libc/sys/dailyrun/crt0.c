#include <unistd.h>

extern int main(int argc, char **argv);

extern char _edata;
extern char _end;

void _start(void)
{
    char *bss;
    int ret;

    /* Fill bss with zeros */
    bss = &_edata + 1;
    while (bss < &_end)
        *bss++ = 0;

    ret = main(0, NULL);
    _exit(ret);
}
