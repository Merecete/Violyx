/* vas on the build computer, for tests: vas_host [-l] [-b BASE] [-e ENTRY] -o OUT file.s ...
 * -l writes the listing (bytes per line) instead of a program. */
#include "../system/cc/vas.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *slurp(const char *n) {
    FILE *f = fopen(n, "rb");
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    long l = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc(l + 1);
    fread(b, 1, l, f);
    b[l] = 0;
    fclose(f);
    return b;
}
int main(int c, char **v) {
    vas_opts o = {0x640000400000ull, "_start", 0};
    const char *out = "a.out";
    vas_file fs[64];
    int n = 0;
    for (int i = 1; i < c; i++) {
        if (!strcmp(v[i], "-l"))
            o.listing = 1;
        else if (!strcmp(v[i], "-b"))
            o.base = strtoull(v[++i], 0, 0);
        else if (!strcmp(v[i], "-e"))
            o.entry = v[++i];
        else if (!strcmp(v[i], "-o"))
            out = v[++i];
        else {
            fs[n].name = v[i];
            fs[n].text = slurp(v[i]);
            if (!fs[n].text) {
                fprintf(stderr, "cannot read %s\n", v[i]);
                return 1;
            }
            n++;
        }
    }
    vas_result r;
    int e = vas_build(fs, n, &o, &r);
    if (e) {
        fprintf(stderr, "%s\n", r.err);
        return 1;
    }
    FILE *f = fopen(out, "wb");
    if (o.listing)
        fwrite(r.list, 1, r.list_len, f);
    else
        fwrite(r.elf, 1, r.elf_len, f);
    fclose(f);
    fprintf(stderr, "text %llu data %llu bss %llu\n", (unsigned long long)r.text_size,
            (unsigned long long)r.data_size, (unsigned long long)r.bss_size);
    return 0;
}
