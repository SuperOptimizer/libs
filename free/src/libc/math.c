/*
 * math.c - Mathematical functions for the free libc.
 * Pure C89. Uses aarch64 FP instructions where possible.
 */

#include <math.h>

/* ------------------------------------------------------------------ */
/* fabs - absolute value (aarch64 fabs instruction)                    */
/* ------------------------------------------------------------------ */

double fabs(double x)
{
    double r;
    __asm__ __volatile__("fabs %d0, %d1" : "=w"(r) : "w"(x));
    return r;
}

/* ------------------------------------------------------------------ */
/* sqrt - square root (aarch64 fsqrt instruction)                      */
/* ------------------------------------------------------------------ */

double sqrt(double x)
{
    double r;
    __asm__ __volatile__("fsqrt %d0, %d1" : "=w"(r) : "w"(x));
    return r;
}

/* ------------------------------------------------------------------ */
/* ceil - round up (aarch64 frintp instruction)                        */
/* ------------------------------------------------------------------ */

double ceil(double x)
{
    double r;
    __asm__ __volatile__("frintp %d0, %d1" : "=w"(r) : "w"(x));
    return r;
}

/* ------------------------------------------------------------------ */
/* floor - round down (aarch64 frintm instruction)                     */
/* ------------------------------------------------------------------ */

double floor(double x)
{
    double r;
    __asm__ __volatile__("frintm %d0, %d1" : "=w"(r) : "w"(x));
    return r;
}

/* ------------------------------------------------------------------ */
/* fmod - floating point remainder                                     */
/* ------------------------------------------------------------------ */

double fmod(double x, double y)
{
    double q;

    if (y == 0.0) {
        return NAN;
    }
    q = x / y;
    /* truncate toward zero */
    if (q >= 0.0) {
        q = floor(q);
    } else {
        q = ceil(q);
    }
    return x - q * y;
}

/* ------------------------------------------------------------------ */
/* frexp - extract mantissa and exponent                                */
/* ------------------------------------------------------------------ */

double frexp(double x, int *exp)
{
    union {
        double d;
        unsigned long u;
    } u;
    int e;

    if (x == 0.0) {
        *exp = 0;
        return 0.0;
    }

    u.d = x;
    /* extract biased exponent (bits 52-62) */
    e = (int)((u.u >> 52) & 0x7FF);
    /* set exponent to bias (1022) for range [0.5, 1.0) */
    *exp = e - 1022;
    u.u = (u.u & 0x800FFFFFFFFFFFFFUL) | 0x3FE0000000000000UL;
    return u.d;
}

/* ------------------------------------------------------------------ */
/* ldexp - multiply by power of 2                                      */
/* ------------------------------------------------------------------ */

double ldexp(double x, int exp)
{
    union {
        double d;
        unsigned long u;
    } u;
    int e;

    if (x == 0.0) {
        return 0.0;
    }

    u.d = x;
    e = (int)((u.u >> 52) & 0x7FF);
    e += exp;
    if (e <= 0) {
        return 0.0;
    }
    if (e >= 0x7FF) {
        return (x > 0.0) ? HUGE_VAL : -HUGE_VAL;
    }
    u.u = (u.u & 0x800FFFFFFFFFFFFFUL) | ((unsigned long)e << 52);
    return u.d;
}

/* ------------------------------------------------------------------ */
/* exp - e^x using range reduction and polynomial                      */
/* ------------------------------------------------------------------ */

/*
 * Constants for exp():
 * ln(2) split into high and low parts for precision
 */
#define LN2_HI  0.6931471805599453
#define LN2_LO  1.9082149292705877e-10
#define INV_LN2 1.4426950408889634

double exp(double x)
{
    double r;
    double t;
    double p;
    int k;

    /* special cases */
    if (x != x) {
        return x; /* NaN */
    }
    if (x > 709.7) {
        return HUGE_VAL;
    }
    if (x < -745.1) {
        return 0.0;
    }
    if (x == 0.0) {
        return 1.0;
    }

    /* reduce: x = k*ln(2) + r, where r in [-ln(2)/2, ln(2)/2] */
    k = (int)(x * INV_LN2 + (x >= 0.0 ? 0.5 : -0.5));
    r = x - (double)k * LN2_HI - (double)k * LN2_LO;

    /* Pade-like approximation: exp(r) ~ 1 + r + r^2/2 + r^3/6 + ... */
    t = r * r;
    p = 1.0 + r + t * 0.5 + t * r * (1.0 / 6.0) +
        t * t * (1.0 / 24.0) + t * t * r * (1.0 / 120.0) +
        t * t * t * (1.0 / 720.0);

    /* multiply by 2^k */
    return ldexp(p, k);
}

/* ------------------------------------------------------------------ */
/* log - natural logarithm                                             */
/* ------------------------------------------------------------------ */

double log(double x)
{
    double f;
    double s;
    double ss;
    double result;
    int k;

    if (x < 0.0) {
        return NAN;
    }
    if (x == 0.0) {
        return -HUGE_VAL;
    }
    if (x != x) {
        return x; /* NaN */
    }

    /* reduce: x = 2^k * f, where f in [0.5, 1.0) */
    f = frexp(x, &k);

    /* shift to [sqrt(0.5), sqrt(2)] range */
    if (f < 0.7071067811865476) {
        f *= 2.0;
        k--;
    }

    /* log(f) = log(1+s) - log(1-s) where s = (f-1)/(f+1) */
    s = (f - 1.0) / (f + 1.0);
    ss = s * s;

    /* Taylor series: 2*s*(1 + s^2/3 + s^4/5 + s^6/7 + ...) */
    result = 2.0 * s * (1.0 + ss * (1.0 / 3.0) +
             ss * ss * (1.0 / 5.0) +
             ss * ss * ss * (1.0 / 7.0) +
             ss * ss * ss * ss * (1.0 / 9.0) +
             ss * ss * ss * ss * ss * (1.0 / 11.0));

    return result + (double)k * 0.6931471805599453;
}

/* ------------------------------------------------------------------ */
/* pow - raise to power                                                */
/* ------------------------------------------------------------------ */

double pow(double x, double y)
{
    /* special cases */
    if (y == 0.0) {
        return 1.0;
    }
    if (x == 1.0) {
        return 1.0;
    }
    if (x == 0.0) {
        if (y > 0.0) {
            return 0.0;
        }
        return HUGE_VAL;
    }

    /* integer exponent fast path */
    if (y == (double)(int)y && y >= 0.0 && y <= 64.0) {
        double result;
        double base;
        int n;

        result = 1.0;
        base = x;
        n = (int)y;
        while (n > 0) {
            if (n & 1) {
                result *= base;
            }
            base *= base;
            n >>= 1;
        }
        return result;
    }

    /* general case: x^y = exp(y * ln(x)) */
    if (x < 0.0) {
        return NAN; /* negative base with non-integer exponent */
    }
    return exp(y * log(x));
}

/* ------------------------------------------------------------------ */
/* sin - sine (Taylor series with range reduction)                     */
/* ------------------------------------------------------------------ */

#define PI      3.14159265358979323846
#define TWO_PI  6.28318530717958647692
#define HALF_PI 1.57079632679489661923

/* reduce angle to [-pi, pi] */
static double _reduce_angle(double x)
{
    if (x >= -PI && x <= PI) {
        return x;
    }
    x = fmod(x, TWO_PI);
    if (x > PI) {
        x -= TWO_PI;
    } else if (x < -PI) {
        x += TWO_PI;
    }
    return x;
}

double sin(double x)
{
    double x2;
    double term;
    double sum;
    int i;

    x = _reduce_angle(x);

    /* Taylor series: x - x^3/3! + x^5/5! - x^7/7! + ... */
    x2 = x * x;
    sum = x;
    term = x;
    for (i = 1; i <= 8; i++) {
        term *= -x2 / (double)((2 * i) * (2 * i + 1));
        sum += term;
    }
    return sum;
}

/* ------------------------------------------------------------------ */
/* cos - cosine                                                        */
/* ------------------------------------------------------------------ */

double cos(double x)
{
    double x2;
    double term;
    double sum;
    int i;

    x = _reduce_angle(x);

    /* Taylor series: 1 - x^2/2! + x^4/4! - x^6/6! + ... */
    x2 = x * x;
    sum = 1.0;
    term = 1.0;
    for (i = 1; i <= 8; i++) {
        term *= -x2 / (double)((2 * i - 1) * (2 * i));
        sum += term;
    }
    return sum;
}

/* ------------------------------------------------------------------ */
/* tan - tangent                                                       */
/* ------------------------------------------------------------------ */

double tan(double x)
{
    double c;

    c = cos(x);
    if (c == 0.0) {
        return HUGE_VAL;
    }
    return sin(x) / c;
}
