/* C library of Violyx: mathematics. sqrt, atan2 and the x87 helpers are in fpu.s. */
#include <math.h>
double __x87_sin(double);
double __x87_cos(double);
double __x87_ylog2x(double x, double y);
double __x87_exp2(double);

double fabs(double x) { return x < 0 ? -x : x == 0 ? 0 : x; }
float fabsf(float x) { return x < 0 ? -x : x; }
double floor(double x) {
    if (x != x || x >= 4503599627370496.0 || x <= -4503599627370496.0)
        return x;
    double t = (double)(long long)x;
    return t > x ? t - 1 : t;
}
double ceil(double x) {
    if (x != x || x >= 4503599627370496.0 || x <= -4503599627370496.0)
        return x;
    double t = (double)(long long)x;
    return t < x ? t + 1 : t;
}
double trunc(double x) { return x < 0 ? ceil(x) : floor(x); }
double round(double x) { return x < 0 ? -floor(-x + 0.5) : floor(x + 0.5); }
long lround(double x) { return (long)round(x); }
float floorf(float x) { return (float)floor(x); }
float ceilf(float x) { return (float)ceil(x); }
float roundf(float x) { return (float)round(x); }
double fmod(double x, double y) {
    if (y == 0 || x != x || y != y)
        return 0.0 / 0.0;
    double q = trunc(x / y), r = x - q * y;
    /* large quotients lose digits: correct the rest */
    while (fabs(r) >= fabs(y))
        r -= trunc(r / y) * y;
    if ((r < 0 && x > 0) || (r > 0 && x < 0))
        r += y;
    return r;
}
float fmodf(float x, float y) { return (float)fmod(x, y); }
static double reduce(double x) { /* to -pi..pi, fsin only works for |x| < 2^63 */
    if (x > 1e9 || x < -1e9)
        x = fmod(x, 2 * M_PI);
    return x;
}
double sin(double x) { return __x87_sin(reduce(x)); }
double cos(double x) { return __x87_cos(reduce(x)); }
double tan(double x) { x = reduce(x); return __x87_sin(x) / __x87_cos(x); }
float sinf(float x) { return (float)sin(x); }
float cosf(float x) { return (float)cos(x); }
float tanf(float x) { return (float)tan(x); }
double atan(double x) { return atan2(x, 1); }
double asin(double x) { return atan2(x, sqrt((1 - x) * (1 + x))); }
double acos(double x) { return atan2(sqrt((1 - x) * (1 + x)), x); }
float atan2f(float y, float x) { return (float)atan2(y, x); }
float atanf(float x) { return (float)atan(x); }
double exp2(double x) {
    if (x != x)
        return x;
    if (x > 1024)
        return 1.0 / 0.0;
    if (x < -1080)
        return 0;
    return __x87_exp2(x);
}
double exp(double x) { return exp2(x * 1.4426950408889634074); }
double log2(double x) {
    if (x < 0 || x != x)
        return 0.0 / 0.0;
    if (x == 0)
        return -1.0 / 0.0;
    return __x87_ylog2x(x, 1);
}
double log(double x) { return log2(x) * 0.69314718055994530942; }
double log10(double x) { return log2(x) * 0.30102999566398119521; }
float expf(float x) { return (float)exp(x); }
float logf(float x) { return (float)log(x); }
double pow(double x, double y) {
    if (y == 0)
        return 1;
    if (x == 0)
        return y > 0 ? 0 : 1.0 / 0.0;
    if (x < 0) {
        double t = trunc(y);
        if (t != y)
            return 0.0 / 0.0;
        double r = exp2(y * log2(-x));
        return fmod(t, 2) != 0 ? -r : r;
    }
    return exp2(y * log2(x));
}
float powf(float x, float y) { return (float)pow(x, y); }
double sinh(double x) { double e = exp(x); return (e - 1 / e) / 2; }
double cosh(double x) { double e = exp(x); return (e + 1 / e) / 2; }
double tanh(double x) {
    if (x > 20) return 1;
    if (x < -20) return -1;
    double e = exp(2 * x);
    return (e - 1) / (e + 1);
}
double hypot(double x, double y) { return sqrt(x * x + y * y); }
double fmin(double a, double b) { return a < b ? a : b; }
double fmax(double a, double b) { return a > b ? a : b; }
double ldexp(double x, int e) { return x * exp2(e); }
double frexp(double x, int *e) {
    *e = 0;
    if (x == 0 || x != x)
        return x;
    double a = fabs(x);
    while (a >= 1) { a /= 2; (*e)++; }
    while (a < 0.5) { a *= 2; (*e)--; }
    return x < 0 ? -a : a;
}
double modf(double x, double *ip) { *ip = trunc(x); return x - *ip; }
int isnan_(double x) { return x != x; }
