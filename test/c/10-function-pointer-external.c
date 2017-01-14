#include <stdio.h>

int main () {
    int (*foo)(const char *);
    foo = &puts;
    return 0;
}
