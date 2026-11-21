#ifndef _VX_MATH_H
#define _VX_MATH_H
double sqrt(double);
double floor(double);
double ceil(double);
double fabs(double);
double fmod(double, double);
double exp(double);
double log(double);
double log2(double);
double pow(double, double);
double round(double);
double trunc(double);
double fmin(double, double);
double fmax(double, double);
#define HUGE_VAL (__builtin_huge_val())
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))
#define M_PI 3.14159265358979323846
#define M_LN2 0.69314718055994530942
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#endif
