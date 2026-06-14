#ifndef MATH_H
#define MATH_H

#define M_PI 3.14159265358979323846
#define M_E 2.71828182845904523536
#define M_SQRT2 1.41421356237309504880
#define M_LN2 0.69314718055994530942
#define M_LN10 2.30258509299404568402

#define HUGE_VAL 1e308
#define INFINITY (1.0/0.0)
#define NAN (0.0/0.0)

double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double sqrt(double x);
double pow(double base, double exp);
double exp(double x);
double log(double x);
double log10(double x);
double log2(double x);
double log1p(double x);
double ceil(double x);
double floor(double x);
double fabs(double x);
double fmod(double x, double y);
double remainder(double x, double y);
double round(double x);
double hypot(double x, double y);
double cbrt(double x);

int isnan(double x);
int isinf(double x);
int isfinite(double x);

#endif
