/* C library of Violyx: strings and memory */
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *a = d;
    const unsigned char *b = s;
    while (n >= 8) {
        *(unsigned long *)a = *(const unsigned long *)b;
        a += 8;
        b += 8;
        n -= 8;
    }
    while (n--)
        *a++ = *b++;
    return d;
}
void *memmove(void *d, const void *s, size_t n) {
    unsigned char *a = d;
    const unsigned char *b = s;
    if (a == b || !n)
        return d;
    if (a < b || a >= b + n)
        return memcpy(d, s, n);
    while (n--)
        a[n] = b[n];
    return d;
}
void *memset(void *d, int c, size_t n) {
    unsigned char *a = d;
    unsigned long v = (unsigned char)c;
    v |= v << 8;
    v |= v << 16;
    v |= v << 32;
    while (n >= 8) {
        *(unsigned long *)a = v;
        a += 8;
        n -= 8;
    }
    while (n--)
        *a++ = (unsigned char)c;
    return d;
}
int memcmp(const void *x, const void *y, size_t n) {
    const unsigned char *a = x, *b = y;
    for (size_t i = 0; i < n; i++)
        if (a[i] != b[i])
            return a[i] - b[i];
    return 0;
}
void *memchr(const void *s, int c, size_t n) {
    const unsigned char *p = s;
    for (size_t i = 0; i < n; i++)
        if (p[i] == (unsigned char)c)
            return (void *)(p + i);
    return 0;
}
size_t strlen(const char *s) {
    const char *p = s;
    while (*p)
        p++;
    return (size_t)(p - s);
}
size_t strnlen(const char *s, size_t n) {
    size_t i = 0;
    while (i < n && s[i])
        i++;
    return i;
}
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b)
        a++, b++;
    return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t n) {
    for (; n; n--, a++, b++) {
        if (*a != *b)
            return (unsigned char)*a - (unsigned char)*b;
        if (!*a)
            return 0;
    }
    return 0;
}
int strcasecmp(const char *a, const char *b) {
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b))
        a++, b++;
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}
int strncasecmp(const char *a, const char *b, size_t n) {
    for (; n; n--, a++, b++) {
        int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b);
        if (x != y)
            return x - y;
        if (!x)
            return 0;
    }
    return 0;
}
char *strcpy(char *d, const char *s) {
    char *r = d;
    while ((*d++ = *s++))
        ;
    return r;
}
char *strncpy(char *d, const char *s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++)
        d[i] = s[i];
    for (; i < n; i++)
        d[i] = 0;
    return d;
}
char *strcat(char *d, const char *s) {
    strcpy(d + strlen(d), s);
    return d;
}
char *strncat(char *d, const char *s, size_t n) {
    char *p = d + strlen(d);
    while (n-- && *s)
        *p++ = *s++;
    *p = 0;
    return d;
}
char *strchr(const char *s, int c) {
    for (;; s++) {
        if (*s == (char)c)
            return (char *)s;
        if (!*s)
            return 0;
    }
}
char *strrchr(const char *s, int c) {
    const char *r = 0;
    for (;; s++) {
        if (*s == (char)c)
            r = s;
        if (!*s)
            return (char *)r;
    }
}
char *strstr(const char *h, const char *n) {
    size_t l = strlen(n);
    if (!l)
        return (char *)h;
    for (; *h; h++)
        if (*h == *n && !strncmp(h, n, l))
            return (char *)h;
    return 0;
}
size_t strspn(const char *s, const char *a) {
    size_t i = 0;
    while (s[i] && strchr(a, s[i]))
        i++;
    return i;
}
size_t strcspn(const char *s, const char *r) {
    size_t i = 0;
    while (s[i] && !strchr(r, s[i]))
        i++;
    return i;
}
char *strpbrk(const char *s, const char *a) {
    s += strcspn(s, a);
    return *s ? (char *)s : 0;
}
char *strtok_r(char *s, const char *d, char **save) {
    if (!s)
        s = *save;
    if (!s)
        return 0;
    s += strspn(s, d);
    if (!*s) {
        *save = 0;
        return 0;
    }
    char *e = s + strcspn(s, d);
    if (*e)
        *e++ = 0;
    else
        e = 0;
    *save = e;
    return s;
}
char *strtok(char *s, const char *d) {
    static char *save;
    return strtok_r(s, d, &save);
}
char *strdup(const char *s) {
    size_t l = strlen(s) + 1;
    char *d = malloc(l);
    if (d)
        memcpy(d, s, l);
    return d;
}
char *strndup(const char *s, size_t n) {
    size_t l = strnlen(s, n);
    char *d = malloc(l + 1);
    if (d) {
        memcpy(d, s, l);
        d[l] = 0;
    }
    return d;
}
char *strerror(int e) {
    switch (e) {
    case 0: return "no error";
    case 2: return "file not found";
    case 12: return "out of memory";
    case 13: return "access denied";
    case 22: return "invalid argument";
    case 28: return "no space left";
    default: return "error";
    }
}
