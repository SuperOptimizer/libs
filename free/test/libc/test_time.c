/*
 * test_time.c - Tests for time.h functions.
 * Pure C89. No external dependencies.
 */

#include "../test.h"
#include <time.h>
#include <string.h>

/* ===== time() tests ===== */

TEST(time_returns_positive)
{
    time_t t;

    t = time(NULL);
    ASSERT(t > 0);
}

TEST(time_fills_pointer)
{
    time_t t1;
    time_t t2;

    t1 = time(&t2);
    ASSERT_EQ(t1, t2);
}

/* ===== clock() tests ===== */

TEST(clock_returns_positive)
{
    clock_t c;

    c = clock();
    ASSERT(c >= 0);
}

/* ===== difftime() tests ===== */

TEST(difftime_basic)
{
    time_t a;
    time_t b;
    double d;

    a = 1000;
    b = 500;
    d = difftime(a, b);
    ASSERT(d > 499.0 && d < 501.0);
}

TEST(difftime_zero)
{
    time_t t;
    double d;

    t = 12345;
    d = difftime(t, t);
    ASSERT(d > -0.1 && d < 0.1);
}

/* ===== gmtime() tests ===== */

TEST(gmtime_epoch)
{
    time_t t;
    struct tm *tm;

    t = 0;
    tm = gmtime(&t);
    ASSERT_NOT_NULL(tm);
    ASSERT_EQ(tm->tm_year, 70);   /* 1970 */
    ASSERT_EQ(tm->tm_mon, 0);     /* January */
    ASSERT_EQ(tm->tm_mday, 1);
    ASSERT_EQ(tm->tm_hour, 0);
    ASSERT_EQ(tm->tm_min, 0);
    ASSERT_EQ(tm->tm_sec, 0);
    ASSERT_EQ(tm->tm_wday, 4);    /* Thursday */
    ASSERT_EQ(tm->tm_yday, 0);
}

TEST(gmtime_known_date)
{
    time_t t;
    struct tm *tm;

    /* 2000-01-01 00:00:00 UTC = 946684800 */
    t = 946684800L;
    tm = gmtime(&t);
    ASSERT_NOT_NULL(tm);
    ASSERT_EQ(tm->tm_year, 100);  /* 2000 */
    ASSERT_EQ(tm->tm_mon, 0);     /* January */
    ASSERT_EQ(tm->tm_mday, 1);
    ASSERT_EQ(tm->tm_wday, 6);    /* Saturday */
}

TEST(gmtime_with_time)
{
    time_t t;
    struct tm *tm;

    /* 1970-01-01 12:30:45 UTC */
    t = 12 * 3600 + 30 * 60 + 45;
    tm = gmtime(&t);
    ASSERT_NOT_NULL(tm);
    ASSERT_EQ(tm->tm_hour, 12);
    ASSERT_EQ(tm->tm_min, 30);
    ASSERT_EQ(tm->tm_sec, 45);
}

/* ===== mktime() tests ===== */

TEST(mktime_epoch)
{
    struct tm tm;
    time_t t;

    memset(&tm, 0, sizeof(tm));
    tm.tm_year = 70;
    tm.tm_mon = 0;
    tm.tm_mday = 1;
    t = mktime(&tm);
    ASSERT_EQ(t, 0);
}

TEST(mktime_known_date)
{
    struct tm tm;
    time_t t;

    memset(&tm, 0, sizeof(tm));
    tm.tm_year = 100; /* 2000 */
    tm.tm_mon = 0;
    tm.tm_mday = 1;
    t = mktime(&tm);
    ASSERT_EQ(t, 946684800L);
}

TEST(mktime_roundtrip)
{
    time_t t1;
    time_t t2;
    struct tm *tm;

    t1 = 1234567890L;
    tm = gmtime(&t1);
    t2 = mktime(tm);
    ASSERT_EQ(t1, t2);
}

/* ===== strftime() tests ===== */

TEST(strftime_basic)
{
    struct tm tm;
    char buf[64];
    size_t n;

    memset(&tm, 0, sizeof(tm));
    tm.tm_year = 123;  /* 2023 */
    tm.tm_mon = 5;     /* June */
    tm.tm_mday = 15;
    tm.tm_hour = 10;
    tm.tm_min = 30;
    tm.tm_sec = 45;

    n = strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    ASSERT(n > 0);
    ASSERT_STR_EQ(buf, "2023-06-15");
}

TEST(strftime_time)
{
    struct tm tm;
    char buf[64];
    size_t n;

    memset(&tm, 0, sizeof(tm));
    tm.tm_hour = 9;
    tm.tm_min = 5;
    tm.tm_sec = 3;

    n = strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    ASSERT(n > 0);
    ASSERT_STR_EQ(buf, "09:05:03");
}

TEST(strftime_weekday_month)
{
    struct tm tm;
    char buf[64];
    size_t n;

    memset(&tm, 0, sizeof(tm));
    tm.tm_wday = 1; /* Monday */
    tm.tm_mon = 11; /* December */

    n = strftime(buf, sizeof(buf), "%a %b", &tm);
    ASSERT(n > 0);
    ASSERT_STR_EQ(buf, "Mon Dec");
}

TEST(strftime_percent)
{
    struct tm tm;
    char buf[16];
    size_t n;

    memset(&tm, 0, sizeof(tm));
    n = strftime(buf, sizeof(buf), "100%%", &tm);
    ASSERT(n > 0);
    ASSERT_STR_EQ(buf, "100%");
}

TEST(strftime_zero_size)
{
    struct tm tm;
    size_t n;

    memset(&tm, 0, sizeof(tm));
    n = strftime(NULL, 0, "%Y", &tm);
    ASSERT_EQ(n, 0);
}

int main(void)
{
    printf("test_time:\n");

    /* time */
    RUN_TEST(time_returns_positive);
    RUN_TEST(time_fills_pointer);

    /* clock */
    RUN_TEST(clock_returns_positive);

    /* difftime */
    RUN_TEST(difftime_basic);
    RUN_TEST(difftime_zero);

    /* gmtime */
    RUN_TEST(gmtime_epoch);
    RUN_TEST(gmtime_known_date);
    RUN_TEST(gmtime_with_time);

    /* mktime */
    RUN_TEST(mktime_epoch);
    RUN_TEST(mktime_known_date);
    RUN_TEST(mktime_roundtrip);

    /* strftime */
    RUN_TEST(strftime_basic);
    RUN_TEST(strftime_time);
    RUN_TEST(strftime_weekday_month);
    RUN_TEST(strftime_percent);
    RUN_TEST(strftime_zero_size);

    TEST_SUMMARY();
    return tests_failed;
}
