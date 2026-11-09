/* Pictures (PNG, JPEG, GIF, BMP) and gzip, decoded inside the program itself: the code of the
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
    if (mode == 0 || mode == 2)
        image(mode, b, (int)n, (int)a1, (int)a2, (uint32_t)a3);
    else if (mode == 1)
        gunzip(b, (long)n, a1);
    kfree(b);
    if (flag)
        *flag = d_flag;
    return d_got ? (long)d_got : VE_FORMAT;
}
