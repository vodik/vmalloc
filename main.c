#include <stdlib.h>
#include <stdio.h>

#include "vmalloc.h"

static const size_t big_size = (1 << 20) + 1;

int main(void)
{
    int *a = allocate(sizeof(int));
    int *b = allocate(32);
    int *c = allocate(sizeof(int));
    int *d = allocate(sizeof(int));

    *a = 1;
    *b = 2;
    *c = 3;
    *d = 4;

    deallocate(c);
    deallocate(d);

    /* intentionally looking for stale memory */
    d = allocate(sizeof(int));
    c = allocate(sizeof(int));

    deallocate(a);
    a = allocate(big_size);
    deallocate_sized(a, big_size);
    a = allocate(sizeof(int));

    printf("a b c d: %d %d %d %d\n", *a, *b, *c, *d);

    deallocate(a);
    deallocate(b);
    deallocate(c);
    deallocate(d);
}
