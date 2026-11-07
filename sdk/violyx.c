#include "violyx.h"
__asm__(
    ".text\n.globl _start\n_start:\n  and $-16,%rsp\n  call main\n  xor %edi,%edi\n  xor %eax,%eax\n  int $48\n1: jmp 1b\n"
    "vx_sys:\n .globl vx_sys\n  mov %rdi,%rax\n  mov %rsi,%rdi\n  mov %rdx,%rsi\n  mov %rcx,%rdx\n  int $48\n  ret\n");
void *memcpy(void *d, const void *s, size_t n) {
    void *r = d;
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n)::"memory");
    return r;
}
void *memset(void *d, int c, size_t n) {
    void *r = d;
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(n) : "a"(c) : "memory");
    return r;
}
void *memmove(void *d, const void *s, size_t n) {
    char *a = d;
    const char *b = s;
    if (a < b)
        while (n--)
            *a++ = *b++;
    else
        while (n--)
            a[n] = b[n];
    return d;
}
size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}
int memcmp(const void *x, const void *y, size_t n) {
    const unsigned char *a = x, *b = y;
    for (size_t i = 0; i < n; i++)
        if (a[i] != b[i])
            return a[i] - b[i];
    return 0;
}
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i] || !a[i])
            return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}
static uint32_t cells[240 * 100];
static int W, H;
static void dims(void) {
    long v = vx_sys(4, 0, 0, 0);
    W = (int)(v >> 16);
    H = (int)(v & 0xFFFF);
}
int vx_cols(void) {
    dims();
    return W;
}
int vx_rows(void) {
    dims();
    return H;
}
void vx_clear(uint8_t a) {
    dims();
    for (int i = 0; i < W * H; i++)
        cells[i] = (uint32_t)' ' | (uint32_t)(a & 15) << 8 | (uint32_t)(a >> 4) << 16;
}
void vx_put(int x, int y, char c, uint8_t a) {
    vx_paint(x, y, c, a & 15, a >> 4);
}
void vx_paint(int x, int y, char c, uint8_t fg, uint8_t bg) {
    if (!W)
        dims();
    if (x >= 0 && x < W && y >= 0 && y < H)
        cells[y * W + x] = (uint32_t)(uint8_t)c | (uint32_t)fg << 8 | (uint32_t)bg << 16;
}
uint8_t vx_rgb(int r, int g, int b) {
    int q = (r + g + b) / 3;
    if (r / 10 == q / 10 && g / 10 == q / 10 && b / 10 == q / 10)
        return (uint8_t)(232 + (q * 23) / 255);
    return (uint8_t)(16 + 36 * (r * 5 / 255) + 6 * (g * 5 / 255) + b * 5 / 255);
}
void vx_text(int x, int y, const char *s, uint8_t a) {
    for (; *s; s++)
        vx_put(x++, y, *s, a);
}
void vx_number(int x, int y, long v, uint8_t a) {
    char b[24];
    int n = 0;
    if (v < 0) {
        vx_put(x++, y, '-', a);
        v = -v;
    }
    do {
        b[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        vx_put(x++, y, b[--n], a);
}
void vx_pixcell(int x, int y, int ix, int iy) {
    if (!W)
        dims();
    if (x >= 0 && x < W && y >= 0 && y < H && ix >= 0 && ix < 240 && iy >= 0 && iy < 100)
        cells[y * W + x] = 0x80000000u | (uint32_t)ix << 8 | (uint32_t)iy << 16;
}
void vx_show(void) {
    vx_sys(18, (long)cells, W, H);
}
void vx_title(const char *s) {
    vx_sys(5, (long)s, 0, 0);
}
void *vx_alloc(size_t n) {
    long p = vx_sys(8, (long)n, 0, 0);
    return p == -1 ? 0 : (void *)p;
}
int vx_random(int n) {
    static uint32_t r;
    if (!r)
        r = (uint32_t)vx_ms() * 2654435761u + vx_time();
    r = r * 1103515245u + 12345u;
    return (int)((r >> 16) % (uint32_t)n);
}
