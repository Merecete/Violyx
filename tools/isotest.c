/* Host test of kernel/iso9660.c: isotest IMAGE  lists every file with size and checksum */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kernel/iso9660.h"
static FILE *img;
static long reads;
static int rd(uint64_t s, void *b) {
    reads++;
    if (fseek(img, (long)s * 512, SEEK_SET))
        return -1;
    return fread(b, 1, 512, img) == 512 ? 0 : -1;
}
static int rdn(uint64_t s, uint32_t n, void *b) {
    reads++;
    if (fseek(img, (long)s * 512, SEEK_SET))
        return -1;
    return fread(b, 512, n, img) == n ? 0 : -1;
}
static isovol v;
static void walk(const char *p);
typedef struct {
    char n[64][300];
    int d[64];
    long s[64];
    int k;
} lst;
static int cb(const vfs_ent *e, void *a) {
    lst *l = a;
    if (l->k < 64) {
        strcpy(l->n[l->k], e->name);
        l->d[l->k] = e->dir;
        l->s[l->k] = (long)e->size;
        l->k++;
    }
    return 0;
}
static void walk(const char *p) {
    lst *l = calloc(1, sizeof *l);
    int r = iso_list(&v, p, cb, l);
    if (r) {
        printf("list %s: %d\n", p, r);
        return;
    }
    for (int i = 0; i < l->k; i++) {
        char q[600];
        snprintf(q, sizeof q, "%s%s%s", p, strcmp(p, "/") ? "/" : "", l->n[i]);
        if (l->d[i]) {
            walk(q);
            continue;
        }
        vfs_ent e;
        if (iso_stat(&v, q, &e)) {
            printf("stat fails: %s\n", q);
            continue;
        }
        unsigned char *b = malloc(e.size + 1);
        long n = (long)iso_read(&v, q, b, e.size);
        unsigned long h = 5381;
        for (long i2 = 0; i2 < n; i2++)
            h = h * 33 + b[i2];
        /* a part in the middle must be the same as the whole read */
        if (e.size > 5000) {
            unsigned char c[3000];
            long m = (long)iso_read_at(&v, q, c, 2047, 3000);
            if (m != 3000 || memcmp(c, b + 2047, 3000))
                printf("read_at differs: %s\n", q);
        }
        printf("%s\t%ld\t%ld\t%lx\n", q, (long)e.size, n, h);
        free(b);
    }
    free(l);
}
int main(int argc, char **argv) {
    img = fopen(argv[1], "rb");
    fseek(img, 0, SEEK_END);
    vfs_dev d = {rd, 0, 0, 0, (uint64_t)ftell(img) / 512, argc > 2 ? 0 : rdn, 0};
    int r = iso_mount(&v, &d);
    printf("mount %d names %d\n", r, v.names);
    if (!r)
        walk("/");
    vfs_ent e;
    printf("stat case-insensitive: %d\n", iso_stat(&v, "/MUSIK", &e));
    return 0;
}
