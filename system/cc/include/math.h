#ifndef __MATH_H
#define __MATH_H
#define M_PI 3.14159265358979323846
#define M_E 2.7182818284590452354
#define M_SQRT2 1.41421356237309504880
#define M_LN2 0.69314718055994530942
#define HUGE_VAL (1.0 / 0.0)
#define INFINITY (1.0f / 0.0f)
#define NAN (0.0f / 0.0f)
#define isnan(x) ((x) != (x))
#define isinf(x) ((x) == INFINITY || (x) == -INFINITY)
#define isfinite(x) (!isnan(x) && !isinf(x))
#define signbit(x) ((x) < 0 || (1 / (double)(x)) < 0)
double sqrt(double);
float sqrtf(float);
double fabs(double);
float fabsf(float);
double floor(double);
double ceil(double);
double trunc(double);
double round(double);
long lround(double);
float floorf(float);
float ceilf(float);
float roundf(float);
double fmod(double, double);
float fmodf(float, float);
double sin(double);
double cos(double);
double tan(double);
float sinf(float);
float cosf(float);
float tanf(float);
double asin(double);
double acos(double);
double atan(double);
double atan2(double, double);
float atanf(float);
float atan2f(float, float);
double exp(double);
double exp2(double);
double log(double);
double log2(double);
double log10(double);
float expf(float);
float logf(float);
double pow(double, double);
float powf(float, float);
double sinh(double);
double cosh(double);
double tanh(double);
double hypot(double, double);
double fmin(double, double);
double fmax(double, double);
double ldexp(double, int);
double frexp(double, int *);
double modf(double, double *);
#endif
