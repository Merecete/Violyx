#pragma once
/* Small maths for programs without a C library. Precise enough for audio decoders and filters. */
#define M_PI 3.14159265358979323846
static inline double fabs(double x) {
    return x < 0 ? -x : x;
}
static inline double floor(double x) {
    double t = (double)(long)x;
    return t > x ? t - 1 : t;
}
static inline double sqrt(double x) {
    double r;
    __asm__("sqrtsd %1,%0" : "=x"(r) : "x"(x));
    return r;
}
static inline double ldexp(double x, int e) {
    while (e > 1000) {
        x *= 0x1p1000;
        e -= 1000;
    }
    while (e < -1000) {
        x *= 0x1p-1000;
        e += 1000;
    }
    union {
        double d;
        unsigned long u;
    } v;
    v.u = (unsigned long)(e + 1023) << 52;
    return x * v.d;
}
static inline double sin(double x) {
    double k = floor(x / (2 * M_PI) + .5);
    x -= k * 2 * M_PI;
    if (x > M_PI / 2)
        x = M_PI - x;
    else if (x < -M_PI / 2)
        x = -M_PI - x;
    double y = x * x;
    return x *
           (1 - y / 6 * (1 - y / 20 * (1 - y / 42 * (1 - y / 72 * (1 - y / 110 * (1 - y / 156))))));
}
static inline double cos(double x) {
    return sin(x + M_PI / 2);
}
static inline double log(double x) {
    if (x <= 0)
        return -1e300;
    union {
        double d;
        unsigned long u;
    } v;
    v.d = x;
    int e = (int)((v.u >> 52) & 2047) - 1023;
    v.u = (v.u & 0xFFFFFFFFFFFFFul) | (1023ul << 52);
    double m = v.d;
    if (m > 1.4142135623730951) {
        m /= 2;
        e++;
    }
    double t = (m - 1) / (m + 1), s = t * t, r = 0;
    for (int i = 19; i >= 1; i -= 2)
        r = r * s + 1.0 / i;
    return 2 * t * r + e * 0.6931471805599453;
}
static inline double exp(double x) {
    int e = (int)floor(x / 0.6931471805599453 + .5);
    double r = x - e * 0.6931471805599453, t = 1, s = 1;
    for (int i = 1; i < 16; i++) {
        t *= r / i;
        s += t;
    }
    return ldexp(s, e);
}
static inline double pow(double a, double b) {
    if (a == 0)
        return 0;
    return exp(b * log(a));
}
