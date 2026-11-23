/* Pictures (PNG, JPEG, GIF, BMP, WebP) and gzip, decoded inside the program itself: the code of the
 * vxdec helper from the SDK, built in. A broken file can at most stop this one program, which
 * runs protected in ring 3. Same interface as the old kernel function. */
#include "os.h"
#define VXDEC_LIB
static u8 *d_out;
static u64 d_max, d_got;
static int d_flag;
static void out(const void *b, long n, int flag) {
    if (n < 0 || (u64)n > d_max)
        return;
    memcpy(d_out, b, (size_t)n);
    d_got = (u64)n;
    d_flag = flag;
}
/* stb_image wants realloc: each block keeps its size in front */
static void *a_malloc(size_t n) {
    u64 *p = kmalloc(n + 16);
    if (!p)
        return 0;
    p[0] = n;
    return p + 2;
}
static void a_free(void *p) {
    if (p)
        kfree((u64 *)p - 2);
}
static void *a_realloc(void *p, size_t n) {
    if (!p)
        return a_malloc(n);
    u64 o = ((u64 *)p)[-2];
    if (n <= o)
        return p;
    void *q = a_malloc(n);
    if (q) {
        memcpy(q, p, (size_t)o);
        a_free(p);
    }
    return q;
}
/* libwebp allocates through these (it is built with -Dmalloc=vxw_malloc and so on) */
void *vxw_malloc(size_t n) {
    return a_malloc(n ? n : 1);
}
void *vxw_calloc(size_t a, size_t b) {
    if (b && a > (size_t)-1 / b)
        return 0;
    void *p = a_malloc(a * b ? a * b : 1);
    if (p)
        memset(p, 0, a * b);
    return p;
}
void vxw_free(void *p) {
    a_free(p);
}
/* Only the palette helper of libwebp sorts, and only when encoding; a small sort is enough */
void qsort(void *b, size_t n, size_t sz, int (*cmp)(const void *, const void *)) {
    u8 *a = b, t[64];
    if (sz > sizeof t)
        return;
    for (size_t i = 1; i < n; i++)
        for (size_t j = i; j > 0 && cmp(a + (j - 1) * sz, a + j * sz) > 0; j--) {
            memcpy(t, a + j * sz, sz);
            memcpy(a + j * sz, a + (j - 1) * sz, sz);
            memcpy(a + (j - 1) * sz, t, sz);
        }
}
#define VXDEC_WEBP
#include "../sdk/helpers/vxdec.c"
long prog_decode(int mode, const u8 *in, u64 n, u8 *o, u64 max, long a1, long a2, long a3, int ms,
                 int *flag) {
    (void)ms;
    if (!n || n > (48ull << 20))
        return VE_FORMAT;
    u8 *b = kmalloc(n + 64); /* the decoders may read a little past the end */
    if (!b)
        return VE_NOSPC;
    memcpy(b, in, n);
    memset(b + n, 0, 64);
    d_out = o;
    d_max = max;
    d_got = 0;
    d_flag = 0;
    if (mode == 0 || mode == 2 || mode == 3)
        image(mode, b, (int)n, (int)a1, (int)a2, (uint32_t)a3);
    else if (mode == 1)
        gunzip(b, (long)n, a1);
    kfree(b);
    if (flag)
        *flag = d_flag;
    return d_got ? (long)d_got : VE_FORMAT;
}
