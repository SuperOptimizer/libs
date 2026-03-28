/*
 * test_math.c - Tests for math.h functions.
 * Pure C89. No external dependencies.
 */

#include "../test.h"
#include <math.h>

/* helper: check if two doubles are close enough */
static int near(double a, double b, double tol)
{
    double diff;

    diff = a - b;
    if (diff < 0.0) {
        diff = -diff;
    }
    return diff < tol;
}

#define ASSERT_NEAR(a, b, tol) \
    ASSERT(near((a), (b), (tol)))

/* ===== fabs tests ===== */

TEST(fabs_positive)
{
    ASSERT_NEAR(fabs(3.14), 3.14, 1e-15);
}

TEST(fabs_negative)
{
    ASSERT_NEAR(fabs(-2.71), 2.71, 1e-15);
}

TEST(fabs_zero)
{
    ASSERT_NEAR(fabs(0.0), 0.0, 1e-15);
}

/* ===== sqrt tests ===== */

TEST(sqrt_four)
{
    ASSERT_NEAR(sqrt(4.0), 2.0, 1e-15);
}

TEST(sqrt_two)
{
    ASSERT_NEAR(sqrt(2.0), 1.41421356237, 1e-10);
}

TEST(sqrt_one)
{
    ASSERT_NEAR(sqrt(1.0), 1.0, 1e-15);
}

TEST(sqrt_zero)
{
    ASSERT_NEAR(sqrt(0.0), 0.0, 1e-15);
}

/* ===== ceil tests ===== */

TEST(ceil_positive_frac)
{
    ASSERT_NEAR(ceil(2.3), 3.0, 1e-15);
}

TEST(ceil_negative_frac)
{
    ASSERT_NEAR(ceil(-2.3), -2.0, 1e-15);
}

TEST(ceil_integer)
{
    ASSERT_NEAR(ceil(5.0), 5.0, 1e-15);
}

/* ===== floor tests ===== */

TEST(floor_positive_frac)
{
    ASSERT_NEAR(floor(2.7), 2.0, 1e-15);
}

TEST(floor_negative_frac)
{
    ASSERT_NEAR(floor(-2.3), -3.0, 1e-15);
}

TEST(floor_integer)
{
    ASSERT_NEAR(floor(5.0), 5.0, 1e-15);
}

/* ===== fmod tests ===== */

TEST(fmod_basic)
{
    ASSERT_NEAR(fmod(5.3, 2.0), 1.3, 1e-10);
}

TEST(fmod_negative)
{
    ASSERT_NEAR(fmod(-5.3, 2.0), -1.3, 1e-10);
}

TEST(fmod_exact)
{
    ASSERT_NEAR(fmod(6.0, 3.0), 0.0, 1e-15);
}

/* ===== frexp tests ===== */

TEST(frexp_basic)
{
    int exp_val;
    double m;

    m = frexp(8.0, &exp_val);
    ASSERT_NEAR(m, 0.5, 1e-15);
    ASSERT_EQ(exp_val, 4); /* 0.5 * 2^4 = 8 */
}

TEST(frexp_one)
{
    int exp_val;
    double m;

    m = frexp(1.0, &exp_val);
    ASSERT_NEAR(m, 0.5, 1e-15);
    ASSERT_EQ(exp_val, 1);
}

TEST(frexp_zero)
{
    int exp_val;
    double m;

    m = frexp(0.0, &exp_val);
    ASSERT_NEAR(m, 0.0, 1e-15);
    ASSERT_EQ(exp_val, 0);
}

/* ===== ldexp tests ===== */

TEST(ldexp_basic)
{
    ASSERT_NEAR(ldexp(1.0, 3), 8.0, 1e-15);
}

TEST(ldexp_half)
{
    ASSERT_NEAR(ldexp(0.5, 4), 8.0, 1e-15);
}

TEST(ldexp_zero)
{
    ASSERT_NEAR(ldexp(0.0, 10), 0.0, 1e-15);
}

/* ===== exp tests ===== */

TEST(exp_zero)
{
    ASSERT_NEAR(exp(0.0), 1.0, 1e-12);
}

TEST(exp_one)
{
    ASSERT_NEAR(exp(1.0), 2.71828182845, 1e-8);
}

TEST(exp_negative)
{
    ASSERT_NEAR(exp(-1.0), 0.367879441171, 1e-8);
}

/* ===== log tests ===== */

TEST(log_one)
{
    ASSERT_NEAR(log(1.0), 0.0, 1e-12);
}

TEST(log_e)
{
    ASSERT_NEAR(log(2.71828182845), 1.0, 1e-8);
}

TEST(log_ten)
{
    ASSERT_NEAR(log(10.0), 2.302585093, 1e-6);
}

/* ===== pow tests ===== */

TEST(pow_square)
{
    ASSERT_NEAR(pow(3.0, 2.0), 9.0, 1e-12);
}

TEST(pow_cube)
{
    ASSERT_NEAR(pow(2.0, 3.0), 8.0, 1e-12);
}

TEST(pow_zero_exp)
{
    ASSERT_NEAR(pow(42.0, 0.0), 1.0, 1e-15);
}

TEST(pow_one_base)
{
    ASSERT_NEAR(pow(1.0, 999.0), 1.0, 1e-15);
}

TEST(pow_fractional)
{
    ASSERT_NEAR(pow(4.0, 0.5), 2.0, 1e-8);
}

/* ===== sin tests ===== */

TEST(sin_zero)
{
    ASSERT_NEAR(sin(0.0), 0.0, 1e-12);
}

TEST(sin_pi_half)
{
    ASSERT_NEAR(sin(1.5707963268), 1.0, 1e-8);
}

TEST(sin_pi)
{
    ASSERT_NEAR(sin(3.14159265359), 0.0, 1e-8);
}

TEST(sin_negative)
{
    ASSERT_NEAR(sin(-1.5707963268), -1.0, 1e-8);
}

/* ===== cos tests ===== */

TEST(cos_zero)
{
    ASSERT_NEAR(cos(0.0), 1.0, 1e-12);
}

TEST(cos_pi_half)
{
    ASSERT_NEAR(cos(1.5707963268), 0.0, 1e-8);
}

TEST(cos_pi)
{
    ASSERT_NEAR(cos(3.14159265359), -1.0, 1e-8);
}

/* ===== tan tests ===== */

TEST(tan_zero)
{
    ASSERT_NEAR(tan(0.0), 0.0, 1e-12);
}

TEST(tan_pi_quarter)
{
    ASSERT_NEAR(tan(0.7853981634), 1.0, 1e-8);
}

int main(void)
{
    printf("test_math:\n");

    /* fabs */
    RUN_TEST(fabs_positive);
    RUN_TEST(fabs_negative);
    RUN_TEST(fabs_zero);

    /* sqrt */
    RUN_TEST(sqrt_four);
    RUN_TEST(sqrt_two);
    RUN_TEST(sqrt_one);
    RUN_TEST(sqrt_zero);

    /* ceil */
    RUN_TEST(ceil_positive_frac);
    RUN_TEST(ceil_negative_frac);
    RUN_TEST(ceil_integer);

    /* floor */
    RUN_TEST(floor_positive_frac);
    RUN_TEST(floor_negative_frac);
    RUN_TEST(floor_integer);

    /* fmod */
    RUN_TEST(fmod_basic);
    RUN_TEST(fmod_negative);
    RUN_TEST(fmod_exact);

    /* frexp */
    RUN_TEST(frexp_basic);
    RUN_TEST(frexp_one);
    RUN_TEST(frexp_zero);

    /* ldexp */
    RUN_TEST(ldexp_basic);
    RUN_TEST(ldexp_half);
    RUN_TEST(ldexp_zero);

    /* exp */
    RUN_TEST(exp_zero);
    RUN_TEST(exp_one);
    RUN_TEST(exp_negative);

    /* log */
    RUN_TEST(log_one);
    RUN_TEST(log_e);
    RUN_TEST(log_ten);

    /* pow */
    RUN_TEST(pow_square);
    RUN_TEST(pow_cube);
    RUN_TEST(pow_zero_exp);
    RUN_TEST(pow_one_base);
    RUN_TEST(pow_fractional);

    /* sin */
    RUN_TEST(sin_zero);
    RUN_TEST(sin_pi_half);
    RUN_TEST(sin_pi);
    RUN_TEST(sin_negative);

    /* cos */
    RUN_TEST(cos_zero);
    RUN_TEST(cos_pi_half);
    RUN_TEST(cos_pi);

    /* tan */
    RUN_TEST(tan_zero);
    RUN_TEST(tan_pi_quarter);

    TEST_SUMMARY();
    return tests_failed;
}
