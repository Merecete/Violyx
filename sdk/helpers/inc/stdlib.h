#pragma once
#include <stddef.h>
static inline int abs(int v) {
    return v < 0 ? -v : v;
}
void *malloc(size_t);
void free(void *);
void *realloc(void *, size_t);
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
