/* The system layer for Linux: only for testing the compiler and the C library on the build
 * computer (programs built with "vcc -linux" run there directly) */
#include "sys.h"
long __syscall(long n, long a, long b, long c);
long __os_write(int fd, const char *b, long n) {
    return __syscall(1, fd, (long)b, n);
}
void __os_exit(int c) {
    __syscall(231, c, 0, 0);
}
static long brk_;
void *__os_sbrk(long n) {
    if (!brk_)
        brk_ = __syscall(12, 0, 0, 0);
    long o = brk_, w = __syscall(12, brk_ + n, 0, 0);
    if (w < brk_ + n)
        return 0;
    brk_ = w;
    return (void *)o;
}
long __os_fsize(const char *p) {
    long fd = __syscall(2, (long)p, 0, 0);
    if (fd < 0)
        return -1;
    long n = __syscall(8, fd, 0, 2); /* lseek to the end */
    __syscall(3, fd, 0, 0);
    return n;
}
long __os_fread(const char *p, void *b, long n) {
    long fd = __syscall(2, (long)p, 0, 0), k = 0;
    if (fd < 0)
        return -1;
    while (k < n) {
        long r = __syscall(0, fd, (long)b + k, n - k);
        if (r <= 0)
            break;
        k += r;
    }
    __syscall(3, fd, 0, 0);
    return k;
}
long __os_fwrite(const char *p, const void *b, long n) {
    long fd = __syscall(2, (long)p, 01101, 0644), k = 0; /* O_WRONLY|O_CREAT|O_TRUNC */
    if (fd < 0)
        return -1;
    while (k < n) {
        long r = __syscall(1, fd, (long)b + k, n - k);
        if (r <= 0)
            break;
        k += r;
    }
    __syscall(3, fd, 0, 0);
    return k == n ? 0 : -1;
}
int __os_remove(const char *p) {
    return __syscall(87, (long)p, 0, 0) ? -1 : 0;
}
int __os_rename(const char *a, const char *b) {
    return __syscall(82, (long)a, (long)b, 0) ? -1 : 0;
}
long __os_time(void) {
    return __syscall(201, 0, 0, 0);
}
long __os_ms(void) {
    long ts[2];
    __syscall(228, 1, (long)ts, 0);
    return ts[0] * 1000 + ts[1] / 1000000;
}
int __os_getchar(void) {
    unsigned char c;
    return __syscall(0, 0, (long)&c, 1) == 1 ? c : -1;
}
void __os_init(void) {
}
void __os_fini(int c) {
    (void)c;
}
int __os_args(char ***argv) {
    static char *a[] = {"program", 0};
    *argv = a;
    return 1;
}
