#include <string.h>

void *memset(void *s, int c, size_t n)
{
    unsigned char *arr = s;
    for (size_t i = 0; i < n; i++) {
        arr[i] = c;
    }
    return s;
}
