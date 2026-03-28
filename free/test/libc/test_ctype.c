/*
 * test_ctype.c - Tests for ctype.h character classification functions.
 * Pure C89. No external dependencies.
 */

#include "../test.h"
#include <ctype.h>

/* ===== isalpha tests ===== */

TEST(isalpha_lowercase)
{
    ASSERT(isalpha('a'));
    ASSERT(isalpha('m'));
    ASSERT(isalpha('z'));
}

TEST(isalpha_uppercase)
{
    ASSERT(isalpha('A'));
    ASSERT(isalpha('M'));
    ASSERT(isalpha('Z'));
}

TEST(isalpha_digits)
{
    ASSERT(!isalpha('0'));
    ASSERT(!isalpha('5'));
    ASSERT(!isalpha('9'));
}

TEST(isalpha_special)
{
    ASSERT(!isalpha(' '));
    ASSERT(!isalpha('\n'));
    ASSERT(!isalpha('!'));
    ASSERT(!isalpha('@'));
    ASSERT(!isalpha('['));
}

TEST(isalpha_boundary)
{
    /* just outside the letter ranges */
    ASSERT(!isalpha('@'));  /* one before 'A' */
    ASSERT(!isalpha('['));  /* one after 'Z' */
    ASSERT(!isalpha('`'));  /* one before 'a' */
    ASSERT(!isalpha('{'));  /* one after 'z' */
    ASSERT(!isalpha(0));
    ASSERT(!isalpha(127));
}

/* ===== isdigit tests ===== */

TEST(isdigit_digits)
{
    ASSERT(isdigit('0'));
    ASSERT(isdigit('5'));
    ASSERT(isdigit('9'));
}

TEST(isdigit_letters)
{
    ASSERT(!isdigit('a'));
    ASSERT(!isdigit('Z'));
}

TEST(isdigit_special)
{
    ASSERT(!isdigit(' '));
    ASSERT(!isdigit('+'));
    ASSERT(!isdigit('-'));
    ASSERT(!isdigit('.'));
}

TEST(isdigit_boundary)
{
    ASSERT(!isdigit('/' )); /* one before '0' */
    ASSERT(!isdigit(':'));  /* one after '9' */
    ASSERT(!isdigit(0));
    ASSERT(!isdigit(127));
}

/* ===== isalnum tests ===== */

TEST(isalnum_alpha)
{
    ASSERT(isalnum('a'));
    ASSERT(isalnum('Z'));
}

TEST(isalnum_digit)
{
    ASSERT(isalnum('0'));
    ASSERT(isalnum('9'));
}

TEST(isalnum_special)
{
    ASSERT(!isalnum(' '));
    ASSERT(!isalnum('!'));
    ASSERT(!isalnum('\t'));
    ASSERT(!isalnum('.'));
}

TEST(isalnum_boundary)
{
    ASSERT(!isalnum('/'));
    ASSERT(!isalnum(':'));
    ASSERT(!isalnum('@'));
    ASSERT(!isalnum('['));
    ASSERT(!isalnum('`'));
    ASSERT(!isalnum('{'));
}

/* ===== isspace tests ===== */

TEST(isspace_whitespace)
{
    ASSERT(isspace(' '));
    ASSERT(isspace('\t'));
    ASSERT(isspace('\n'));
    ASSERT(isspace('\r'));
    ASSERT(isspace('\f'));
    ASSERT(isspace('\v'));
}

TEST(isspace_non_whitespace)
{
    ASSERT(!isspace('a'));
    ASSERT(!isspace('0'));
    ASSERT(!isspace('!'));
    ASSERT(!isspace('\0'));
}

TEST(isspace_boundary)
{
    ASSERT(!isspace(0));
    ASSERT(!isspace(127));
    ASSERT(!isspace('A'));
}

/* ===== isupper tests ===== */

TEST(isupper_uppercase)
{
    ASSERT(isupper('A'));
    ASSERT(isupper('M'));
    ASSERT(isupper('Z'));
}

TEST(isupper_lowercase)
{
    ASSERT(!isupper('a'));
    ASSERT(!isupper('m'));
    ASSERT(!isupper('z'));
}

TEST(isupper_other)
{
    ASSERT(!isupper('0'));
    ASSERT(!isupper(' '));
    ASSERT(!isupper('!'));
}

TEST(isupper_boundary)
{
    ASSERT(!isupper('@'));  /* one before 'A' */
    ASSERT(!isupper('['));  /* one after 'Z' */
    ASSERT(!isupper(0));
    ASSERT(!isupper(127));
}

/* ===== islower tests ===== */

TEST(islower_lowercase)
{
    ASSERT(islower('a'));
    ASSERT(islower('m'));
    ASSERT(islower('z'));
}

TEST(islower_uppercase)
{
    ASSERT(!islower('A'));
    ASSERT(!islower('M'));
    ASSERT(!islower('Z'));
}

TEST(islower_other)
{
    ASSERT(!islower('0'));
    ASSERT(!islower(' '));
    ASSERT(!islower('!'));
}

TEST(islower_boundary)
{
    ASSERT(!islower('`'));  /* one before 'a' */
    ASSERT(!islower('{'));  /* one after 'z' */
    ASSERT(!islower(0));
    ASSERT(!islower(127));
}

/* ===== toupper tests ===== */

TEST(toupper_lowercase)
{
    ASSERT_EQ(toupper('a'), 'A');
    ASSERT_EQ(toupper('m'), 'M');
    ASSERT_EQ(toupper('z'), 'Z');
}

TEST(toupper_uppercase)
{
    /* already uppercase: should not change */
    ASSERT_EQ(toupper('A'), 'A');
    ASSERT_EQ(toupper('Z'), 'Z');
}

TEST(toupper_digits)
{
    ASSERT_EQ(toupper('0'), '0');
    ASSERT_EQ(toupper('9'), '9');
}

TEST(toupper_special)
{
    ASSERT_EQ(toupper(' '), ' ');
    ASSERT_EQ(toupper('!'), '!');
    ASSERT_EQ(toupper('\n'), '\n');
}

TEST(toupper_boundary)
{
    ASSERT_EQ(toupper('`'), '`');  /* one before 'a' */
    ASSERT_EQ(toupper('{'), '{');  /* one after 'z' */
    ASSERT_EQ(toupper(0), 0);
    ASSERT_EQ(toupper(127), 127);
}

/* ===== tolower tests ===== */

TEST(tolower_uppercase)
{
    ASSERT_EQ(tolower('A'), 'a');
    ASSERT_EQ(tolower('M'), 'm');
    ASSERT_EQ(tolower('Z'), 'z');
}

TEST(tolower_lowercase)
{
    /* already lowercase: should not change */
    ASSERT_EQ(tolower('a'), 'a');
    ASSERT_EQ(tolower('z'), 'z');
}

TEST(tolower_digits)
{
    ASSERT_EQ(tolower('0'), '0');
    ASSERT_EQ(tolower('9'), '9');
}

TEST(tolower_special)
{
    ASSERT_EQ(tolower(' '), ' ');
    ASSERT_EQ(tolower('!'), '!');
    ASSERT_EQ(tolower('\n'), '\n');
}

TEST(tolower_boundary)
{
    ASSERT_EQ(tolower('@'), '@');  /* one before 'A' */
    ASSERT_EQ(tolower('['), '[');  /* one after 'Z' */
    ASSERT_EQ(tolower(0), 0);
    ASSERT_EQ(tolower(127), 127);
}

/* ===== isprint tests ===== */

TEST(isprint_printable)
{
    ASSERT(isprint(' '));
    ASSERT(isprint('A'));
    ASSERT(isprint('~'));
    ASSERT(isprint('0'));
    ASSERT(isprint('!'));
}

TEST(isprint_control)
{
    ASSERT(!isprint('\0'));
    ASSERT(!isprint('\n'));
    ASSERT(!isprint('\t'));
    ASSERT(!isprint(0x1F));
    ASSERT(!isprint(0x7F));
}

TEST(isprint_boundary)
{
    ASSERT(isprint(0x20));   /* space, first printable */
    ASSERT(isprint(0x7E));   /* '~', last printable */
    ASSERT(!isprint(0x1F));  /* one before space */
    ASSERT(!isprint(0x7F));  /* DEL, one after '~' */
}

/* ===== ispunct tests ===== */

TEST(ispunct_punctuation)
{
    ASSERT(ispunct('!'));
    ASSERT(ispunct('.'));
    ASSERT(ispunct(','));
    ASSERT(ispunct('@'));
    ASSERT(ispunct('#'));
}

TEST(ispunct_not_punct)
{
    ASSERT(!ispunct('a'));
    ASSERT(!ispunct('Z'));
    ASSERT(!ispunct('0'));
    ASSERT(!ispunct(' '));
    ASSERT(!ispunct('\n'));
}

/* ===== isxdigit tests ===== */

TEST(isxdigit_digits)
{
    ASSERT(isxdigit('0'));
    ASSERT(isxdigit('9'));
}

TEST(isxdigit_hex_upper)
{
    ASSERT(isxdigit('A'));
    ASSERT(isxdigit('F'));
    ASSERT(!isxdigit('G'));
}

TEST(isxdigit_hex_lower)
{
    ASSERT(isxdigit('a'));
    ASSERT(isxdigit('f'));
    ASSERT(!isxdigit('g'));
}

TEST(isxdigit_non_hex)
{
    ASSERT(!isxdigit(' '));
    ASSERT(!isxdigit('x'));
    ASSERT(!isxdigit('!'));
}

/* ===== full range sweep tests ===== */

TEST(ctype_zero)
{
    /* null byte should not be alpha, digit, upper, lower */
    ASSERT(!isalpha(0));
    ASSERT(!isdigit(0));
    ASSERT(!isalnum(0));
    ASSERT(!isspace(0));
    ASSERT(!isupper(0));
    ASSERT(!islower(0));
    ASSERT(!isprint(0));
}

TEST(ctype_all_digits)
{
    int c;
    for (c = '0'; c <= '9'; c++) {
        ASSERT(isdigit(c));
        ASSERT(isalnum(c));
        ASSERT(isxdigit(c));
        ASSERT(!isalpha(c));
        ASSERT(!isupper(c));
        ASSERT(!islower(c));
        ASSERT(!isspace(c));
    }
}

TEST(ctype_all_upper)
{
    int c;
    for (c = 'A'; c <= 'Z'; c++) {
        ASSERT(isalpha(c));
        ASSERT(isupper(c));
        ASSERT(!islower(c));
        ASSERT(isalnum(c));
        ASSERT_EQ(tolower(c), c + 32);
    }
}

TEST(ctype_all_lower)
{
    int c;
    for (c = 'a'; c <= 'z'; c++) {
        ASSERT(isalpha(c));
        ASSERT(islower(c));
        ASSERT(!isupper(c));
        ASSERT(isalnum(c));
        ASSERT_EQ(toupper(c), c - 32);
    }
}

int main(void)
{
    printf("test_ctype:\n");

    /* isalpha */
    RUN_TEST(isalpha_lowercase);
    RUN_TEST(isalpha_uppercase);
    RUN_TEST(isalpha_digits);
    RUN_TEST(isalpha_special);
    RUN_TEST(isalpha_boundary);

    /* isdigit */
    RUN_TEST(isdigit_digits);
    RUN_TEST(isdigit_letters);
    RUN_TEST(isdigit_special);
    RUN_TEST(isdigit_boundary);

    /* isalnum */
    RUN_TEST(isalnum_alpha);
    RUN_TEST(isalnum_digit);
    RUN_TEST(isalnum_special);
    RUN_TEST(isalnum_boundary);

    /* isspace */
    RUN_TEST(isspace_whitespace);
    RUN_TEST(isspace_non_whitespace);
    RUN_TEST(isspace_boundary);

    /* isupper */
    RUN_TEST(isupper_uppercase);
    RUN_TEST(isupper_lowercase);
    RUN_TEST(isupper_other);
    RUN_TEST(isupper_boundary);

    /* islower */
    RUN_TEST(islower_lowercase);
    RUN_TEST(islower_uppercase);
    RUN_TEST(islower_other);
    RUN_TEST(islower_boundary);

    /* toupper */
    RUN_TEST(toupper_lowercase);
    RUN_TEST(toupper_uppercase);
    RUN_TEST(toupper_digits);
    RUN_TEST(toupper_special);
    RUN_TEST(toupper_boundary);

    /* tolower */
    RUN_TEST(tolower_uppercase);
    RUN_TEST(tolower_lowercase);
    RUN_TEST(tolower_digits);
    RUN_TEST(tolower_special);
    RUN_TEST(tolower_boundary);

    /* isprint */
    RUN_TEST(isprint_printable);
    RUN_TEST(isprint_control);
    RUN_TEST(isprint_boundary);

    /* ispunct */
    RUN_TEST(ispunct_punctuation);
    RUN_TEST(ispunct_not_punct);

    /* isxdigit */
    RUN_TEST(isxdigit_digits);
    RUN_TEST(isxdigit_hex_upper);
    RUN_TEST(isxdigit_hex_lower);
    RUN_TEST(isxdigit_non_hex);

    /* sweep tests */
    RUN_TEST(ctype_zero);
    RUN_TEST(ctype_all_digits);
    RUN_TEST(ctype_all_upper);
    RUN_TEST(ctype_all_lower);

    TEST_SUMMARY();
    return tests_failed;
}
