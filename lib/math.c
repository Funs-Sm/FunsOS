#include "math.h"

static double normalize_angle(double x) {
    while (x > M_PI) x -= 2.0 * M_PI;
    while (x < -M_PI) x += 2.0 * M_PI;
    return x;
}

double sin(double x) {
    x = normalize_angle(x);
    double result = 0.0;
    double term = x;
    for (int i = 1; i <= 10; i++) {
        result += term;
        term *= -x * x / (double)(2 * i * (2 * i + 1));
    }
    return result;
}

double cos(double x) {
    x = normalize_angle(x);
    double result = 0.0;
    double term = 1.0;
    for (int i = 1; i <= 10; i++) {
        result += term;
        term *= -x * x / (double)((2 * i - 1) * (2 * i));
    }
    return result;
}

double tan(double x) {
    double c = cos(x);
    if (c == 0.0) return 0.0;
    return sin(x) / c;
}

double asin(double x) {
    if (x < -1.0 || x > 1.0) return 0.0;
    double result = x;
    double term = x;
    double x2 = x * x;
    for (int i = 1; i <= 20; i++) {
        term *= x2 * (2 * i - 1) * (2 * i - 1) / (double)(2 * i * (2 * i + 1));
        result += term;
    }
    return result;
}

double acos(double x) {
    return M_PI / 2.0 - asin(x);
}

double atan(double x) {
    if (x > 1.0) return M_PI / 2.0 - atan(1.0 / x);
    if (x < -1.0) return -M_PI / 2.0 - atan(1.0 / x);
    double result = 0.0;
    double term = x;
    double x2 = x * x;
    for (int i = 1; i <= 30; i++) {
        result += term / (double)(2 * i - 1);
        term *= -x2;
    }
    return result;
}

double atan2(double y, double x) {
    if (x > 0.0) return atan(y / x);
    if (x < 0.0 && y >= 0.0) return atan(y / x) + M_PI;
    if (x < 0.0 && y < 0.0) return atan(y / x) - M_PI;
    if (x == 0.0 && y > 0.0) return M_PI / 2.0;
    if (x == 0.0 && y < 0.0) return -M_PI / 2.0;
    return 0.0;
}

double sqrt(double x) {
    if (x < 0.0) return 0.0;
    if (x == 0.0) return 0.0;
    double guess = x / 2.0;
    for (int i = 0; i < 50; i++) {
        double next = (guess + x / guess) / 2.0;
        if (next == guess) break;
        guess = next;
    }
    return guess;
}

double exp(double x) {
    double result = 1.0;
    double term = 1.0;
    for (int i = 1; i <= 30; i++) {
        term *= x / (double)i;
        result += term;
    }
    return result;
}

double log(double x) {
    if (x <= 0.0) return 0.0;
    double y = (x - 1.0) / (x + 1.0);
    double y2 = y * y;
    double result = 0.0;
    double term = y;
    for (int i = 1; i <= 50; i += 2) {
        result += term / (double)i;
        term *= y2;
    }
    return 2.0 * result;
}

double log10(double x) {
    return log(x) / 2.302585092994046;
}

double pow(double base, double exp_val) {
    if (base <= 0.0) return 0.0;
    return exp(exp_val * log(base));
}

double ceil(double x) {
    long i = (long)x;
    if (x > 0.0 && (double)i != x) i++;
    return (double)i;
}

double floor(double x) {
    long i = (long)x;
    if (x < 0.0 && (double)i != x) i--;
    return (double)i;
}

double fabs(double x) {
    return x < 0.0 ? -x : x;
}

double fmod(double x, double y) {
    if (y == 0.0) return 0.0;
    long quotient = (long)(x / y);
    return x - (double)quotient * y;
}

double remainder(double x, double y) {
    if (y == 0.0) return 0.0;
    double q = x / y;
    long n = (long)q;
    double r = q - (double)n;
    /* Round to nearest even */
    if (r > 0.5 || (r == 0.5 && (n & 1))) n++;
    else if (r < -0.5 || (r == -0.5 && (n & 1))) n--;
    return x - (double)n * y;
}

double round(double x) {
    return floor(x + 0.5);
}

double hypot(double x, double y) {
    double ax = fabs(x);
    double ay = fabs(y);
    if (ax > ay) {
        double t = ay / ax;
        return ax * sqrt(1.0 + t * t);
    } else if (ay > 0.0) {
        double t = ax / ay;
        return ay * sqrt(1.0 + t * t);
    }
    return 0.0;
}

double log2(double x) {
    return log(x) / M_LN2;
}

double log1p(double x) {
    if (fabs(x) < 1e-8) return x;
    return log(1.0 + x) * x / ((1.0 + x) - 1.0) * (1.0 / x);
}

double cbrt(double x) {
    if (x == 0.0) return 0.0;
    int negative = x < 0.0;
    if (negative) x = -x;
    double guess = x / 3.0;
    for (int i = 0; i < 50; i++) {
        double next = (2.0 * guess + x / (guess * guess)) / 3.0;
        if (fabs(next - guess) < 1e-15) break;
        guess = next;
    }
    return negative ? -guess : guess;
}

int isnan(double x) {
    (void)x;
    /* Portable NaN check: x != x is true only for NaN */
    return x != x;
}

int isinf(double x) {
    (void)x;
    /* Portable infinity check */
    if (x != x) return 0; /* NaN */
    return x > 1e308 || x < -1e308;
}

int isfinite(double x) {
    return !isnan(x) && !isinf(x);
}

float sinf(float x) { return (float)sin((double)x); }
float cosf(float x) { return (float)cos((double)x); }
float tanf(float x) { return (float)tan((double)x); }
float sqrtf(float x) { return (float)sqrt((double)x); }
float atan2f(float y, float x) { return (float)atan2((double)y, (double)x); }
