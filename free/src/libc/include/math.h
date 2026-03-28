/*
 * math.h - Mathematical functions.
 * Pure C89.
 */

#ifndef _MATH_H
#define _MATH_H

#define HUGE_VAL (__builtin_huge_val())
#define INFINITY (__builtin_inf())
#define NAN      (__builtin_nan(""))

double fabs(double x);
double sqrt(double x);
double sin(double x);
double cos(double x);
double tan(double x);
double log(double x);
double exp(double x);
double pow(double x, double y);
double ceil(double x);
double floor(double x);
double fmod(double x, double y);
double frexp(double x, int *exp);
double ldexp(double x, int exp);

#endif
