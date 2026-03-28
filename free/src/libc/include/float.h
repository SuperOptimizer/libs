#ifndef _FLOAT_H
#define _FLOAT_H

/* Standard float characteristics for IEEE 754 on aarch64 */

#define FLT_RADIX      2
#define FLT_ROUNDS     1  /* round to nearest */

/* float: 32-bit IEEE 754 single precision */
#define FLT_MANT_DIG   24
#define FLT_DIG        6
#define FLT_MIN_EXP    (-125)
#define FLT_MAX_EXP    128
#define FLT_MIN_10_EXP (-37)
#define FLT_MAX_10_EXP 38
#define FLT_MIN        1.17549435e-38F
#define FLT_MAX        3.40282347e+38F
#define FLT_EPSILON    1.19209290e-07F

/* double: 64-bit IEEE 754 double precision */
#define DBL_MANT_DIG   53
#define DBL_DIG        15
#define DBL_MIN_EXP    (-1021)
#define DBL_MAX_EXP    1024
#define DBL_MIN_10_EXP (-307)
#define DBL_MAX_10_EXP 308
#define DBL_MIN        2.2250738585072014e-308
#define DBL_MAX        1.7976931348623157e+308
#define DBL_EPSILON    2.2204460492503131e-16

/* long double: 128-bit IEEE 754 quad precision on aarch64 */
#define LDBL_MANT_DIG  113
#define LDBL_DIG       33
#define LDBL_MIN_EXP   (-16381)
#define LDBL_MAX_EXP   16384
#define LDBL_MIN_10_EXP (-4931)
#define LDBL_MAX_10_EXP 4932
#define LDBL_MIN       3.36210314311209350626267781732175260e-4932L
#define LDBL_MAX       1.18973149535723176508575932662800702e+4932L
#define LDBL_EPSILON   1.92592994438723585305597794258492732e-34L

/* C11 decimal digit macros */
#define DECIMAL_DIG    36

#endif
