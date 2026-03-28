// test_argparse.c — Tests for argparse.h
// Compile: cc -std=c23 -Wall -Wextra -Werror -O2 test_argparse.c -o test_argparse

#define AP_IMPLEMENTATION
#include "argparse.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ── Test Harness ────────────────────────────────────────────────────────────

static int tests_run    = 0;
static int tests_passed = 0;

#define TEST(name) static void name(void)
#define RUN(name)                                                    \
    do {                                                             \
        tests_run++;                                                 \
        printf("  %-50s ", #name);                                   \
        name();                                                      \
        tests_passed++;                                              \
        printf("PASS\n");                                            \
    } while (0)

#define ASSERT(cond)                                                 \
    do {                                                             \
        if (!(cond)) {                                               \
            printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                 \
        }                                                            \
    } while (0)

#define ASSERT_EQ_INT(a, b)                                          \
    do {                                                             \
        int64_t a_ = (a), b_ = (b);                                 \
        if (a_ != b_) {                                              \
            printf("FAIL\n    %s:%d: %" PRId64 " != %" PRId64 "\n", \
                   __FILE__, __LINE__, a_, b_);                      \
            exit(1);                                                 \
        }                                                            \
    } while (0)

#define ASSERT_EQ_STR(a, b)                                          \
    do {                                                             \
        const char* a_ = (a); const char* b_ = (b);                 \
        if (a_ == NULL || b_ == NULL || strcmp(a_, b_) != 0) {       \
            printf("FAIL\n    %s:%d: \"%s\" != \"%s\"\n",           \
                   __FILE__, __LINE__, a_ ? a_ : "(null)",           \
                   b_ ? b_ : "(null)");                              \
            exit(1);                                                 \
        }                                                            \
    } while (0)

#define ASSERT_NEAR(a, b, eps)                                       \
    do {                                                             \
        double a_ = (a), b_ = (b);                                  \
        if (fabs(a_ - b_) > (eps)) {                                 \
            printf("FAIL\n    %s:%d: %f != %f\n",                   \
                   __FILE__, __LINE__, a_, b_);                      \
            exit(1);                                                 \
        }                                                            \
    } while (0)

// ── Helpers ─────────────────────────────────────────────────────────────────

// Build a fresh parser, run a test, then free it.
// Each test constructs its own argv for clarity.

// ── Tests ───────────────────────────────────────────────────────────────────

TEST(test_string_option) {
    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "prog", "desc") == AP_OK);
    ASSERT(ap_add_string(p, "output", 'o', "Output file", "out.txt") == AP_OK);

    char* argv[] = {"prog", "--output", "result.bin"};
    ASSERT(ap_parse(p, 3, argv) == AP_OK);
    ASSERT_EQ_STR(ap_get_string(p, "output"), "result.bin");
    ap_free(p);
}

TEST(test_int_option) {
    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "prog", "desc") == AP_OK);
    ASSERT(ap_add_int(p, "count", 'n', "Count", 10) == AP_OK);

    char* argv[] = {"prog", "--count", "42"};
    ASSERT(ap_parse(p, 3, argv) == AP_OK);
    ASSERT_EQ_INT(ap_get_int(p, "count"), 42);
    ap_free(p);
}

TEST(test_float_option) {
    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "prog", "desc") == AP_OK);
    ASSERT(ap_add_float(p, "rate", 'r', "Learning rate", 0.01) == AP_OK);

    char* argv[] = {"prog", "--rate", "3.14"};
    ASSERT(ap_parse(p, 3, argv) == AP_OK);
    ASSERT_NEAR(ap_get_float(p, "rate"), 3.14, 1e-9);
    ap_free(p);
}

TEST(test_bool_option) {
    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "prog", "desc") == AP_OK);
    ASSERT(ap_add_bool(p, "verbose", 'v', "Verbose output") == AP_OK);

    char* argv[] = {"prog", "--verbose"};
    ASSERT(ap_parse(p, 2, argv) == AP_OK);
    ASSERT(ap_get_bool(p, "verbose") == true);
    ap_free(p);
}

TEST(test_bool_not_set) {
    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "prog", "desc") == AP_OK);
    ASSERT(ap_add_bool(p, "verbose", 'v', "Verbose output") == AP_OK);

    char* argv[] = {"prog"};
    ASSERT(ap_parse(p, 1, argv) == AP_OK);
    ASSERT(ap_get_bool(p, "verbose") == false);
    ap_free(p);
}

TEST(test_short_options) {
    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "prog", "desc") == AP_OK);
    ASSERT(ap_add_string(p, "output", 'o', "Output file", NULL) == AP_OK);
    ASSERT(ap_add_int(p, "count", 'n', "Count", 0) == AP_OK);

    char* argv[] = {"prog", "-o", "file.txt", "-n", "7"};
    ASSERT(ap_parse(p, 5, argv) == AP_OK);
    ASSERT_EQ_STR(ap_get_string(p, "output"), "file.txt");
    ASSERT_EQ_INT(ap_get_int(p, "count"), 7);
    ap_free(p);
}

TEST(test_short_option_attached_value) {
    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "prog", "desc") == AP_OK);
    ASSERT(ap_add_string(p, "output", 'o', "Output file", NULL) == AP_OK);

    char* argv[] = {"prog", "-ofile.txt"};
    ASSERT(ap_parse(p, 2, argv) == AP_OK);
    ASSERT_EQ_STR(ap_get_string(p, "output"), "file.txt");
    ap_free(p);
}

TEST(test_key_equals_value) {
    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "prog", "desc") == AP_OK);
    ASSERT(ap_add_string(p, "output", 'o', "Output file", NULL) == AP_OK);
    ASSERT(ap_add_int(p, "count", 'n', "Count", 0) == AP_OK);

    char* argv[] = {"prog", "--output=result.bin", "--count=99"};
    ASSERT(ap_parse(p, 3, argv) == AP_OK);
    ASSERT_EQ_STR(ap_get_string(p, "output"), "result.bin");
    ASSERT_EQ_INT(ap_get_int(p, "count"), 99);
    ap_free(p);
}

TEST(test_positional_args) {
    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "prog", "desc") == AP_OK);
    ASSERT(ap_add_positional(p, "input", "Input file") == AP_OK);
    ASSERT(ap_add_positional(p, "output", "Output file") == AP_OK);

    char* argv[] = {"prog", "in.txt", "out.txt"};
    ASSERT(ap_parse(p, 3, argv) == AP_OK);
    ASSERT_EQ_INT(ap_positional_count(p), 2);
    ASSERT_EQ_STR(ap_get_positional(p, 0), "in.txt");
    ASSERT_EQ_STR(ap_get_positional(p, 1), "out.txt");
    ap_free(p);
}

TEST(test_required_missing) {
    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "prog", "desc") == AP_OK);
    ASSERT(ap_add_string(p, "input", 'i', "Input file", NULL) == AP_OK);
    ASSERT(ap_required(p, "input") == AP_OK);

    char* argv[] = {"prog"};
    ASSERT(ap_parse(p, 1, argv) == AP_ERR_REQUIRED);
    ap_free(p);
}

TEST(test_required_provided) {
    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "prog", "desc") == AP_OK);
    ASSERT(ap_add_string(p, "input", 'i', "Input file", NULL) == AP_OK);
    ASSERT(ap_required(p, "input") == AP_OK);

    char* argv[] = {"prog", "--input", "data.bin"};
    ASSERT(ap_parse(p, 3, argv) == AP_OK);
    ASSERT_EQ_STR(ap_get_string(p, "input"), "data.bin");
    ap_free(p);
}

TEST(test_unknown_option) {
    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "prog", "desc") == AP_OK);
    ASSERT(ap_add_bool(p, "verbose", 'v', "Verbose") == AP_OK);

    char* argv[] = {"prog", "--unknown"};
    ASSERT(ap_parse(p, 2, argv) == AP_ERR_UNKNOWN_OPT);
    ap_free(p);
}

TEST(test_unknown_short_option) {
    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "prog", "desc") == AP_OK);
    ASSERT(ap_add_bool(p, "verbose", 'v', "Verbose") == AP_OK);

    char* argv[] = {"prog", "-x"};
    ASSERT(ap_parse(p, 2, argv) == AP_ERR_UNKNOWN_OPT);
    ap_free(p);
}

TEST(test_default_values) {
    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "prog", "desc") == AP_OK);
    ASSERT(ap_add_string(p, "output", 'o', "Output", "default.txt") == AP_OK);
    ASSERT(ap_add_int(p, "count", 'n', "Count", 42) == AP_OK);
    ASSERT(ap_add_float(p, "rate", 'r', "Rate", 2.718) == AP_OK);
    ASSERT(ap_add_bool(p, "verbose", 'v', "Verbose") == AP_OK);

    char* argv[] = {"prog"};
    ASSERT(ap_parse(p, 1, argv) == AP_OK);
    ASSERT_EQ_STR(ap_get_string(p, "output"), "default.txt");
    ASSERT_EQ_INT(ap_get_int(p, "count"), 42);
    ASSERT_NEAR(ap_get_float(p, "rate"), 2.718, 1e-9);
    ASSERT(ap_get_bool(p, "verbose") == false);
    ap_free(p);
}

TEST(test_stacked_short_booleans) {
    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "prog", "desc") == AP_OK);
    ASSERT(ap_add_bool(p, "all", 'a', "All") == AP_OK);
    ASSERT(ap_add_bool(p, "brief", 'b', "Brief") == AP_OK);
    ASSERT(ap_add_bool(p, "color", 'c', "Color") == AP_OK);

    char* argv[] = {"prog", "-abc"};
    ASSERT(ap_parse(p, 2, argv) == AP_OK);
    ASSERT(ap_get_bool(p, "all") == true);
    ASSERT(ap_get_bool(p, "brief") == true);
    ASSERT(ap_get_bool(p, "color") == true);
    ap_free(p);
}

TEST(test_double_dash_separator) {
    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "prog", "desc") == AP_OK);
    ASSERT(ap_add_bool(p, "verbose", 'v', "Verbose") == AP_OK);
    ASSERT(ap_add_positional(p, "file", "File") == AP_OK);

    char* argv[] = {"prog", "-v", "--", "--not-an-option", "-x"};
    ASSERT(ap_parse(p, 5, argv) == AP_OK);
    ASSERT(ap_get_bool(p, "verbose") == true);
    ASSERT_EQ_INT(ap_positional_count(p), 2);
    ASSERT_EQ_STR(ap_get_positional(p, 0), "--not-an-option");
    ASSERT_EQ_STR(ap_get_positional(p, 1), "-x");
    ap_free(p);
}

TEST(test_help_output) {
    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "mytool", "A test tool for processing data.") == AP_OK);
    ASSERT(ap_add_string(p, "input", 'i', "Input file", NULL) == AP_OK);
    ASSERT(ap_add_bool(p, "verbose", 'v', "Enable verbose output") == AP_OK);
    ASSERT(ap_add_positional(p, "output", "Output file") == AP_OK);

    // Just verify it doesn't crash; write to a temporary file to capture
    FILE* f = tmpfile();
    ASSERT(f != NULL);
    ap_print_help(p, f);

    // Verify something was written
    long len = ftell(f);
    ASSERT(len > 0);
    fclose(f);
    ap_free(p);
}

TEST(test_is_set) {
    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "prog", "desc") == AP_OK);
    ASSERT(ap_add_string(p, "output", 'o', "Output", "default.txt") == AP_OK);
    ASSERT(ap_add_int(p, "count", 'n', "Count", 10) == AP_OK);

    char* argv[] = {"prog", "--output", "out.bin"};
    ASSERT(ap_parse(p, 3, argv) == AP_OK);
    ASSERT(ap_is_set(p, "output") == true);
    ASSERT(ap_is_set(p, "count") == false);
    ap_free(p);
}

TEST(test_missing_value_long) {
    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "prog", "desc") == AP_OK);
    ASSERT(ap_add_string(p, "output", 'o', "Output", NULL) == AP_OK);

    char* argv[] = {"prog", "--output"};
    ASSERT(ap_parse(p, 2, argv) == AP_ERR_MISSING_VAL);
    ap_free(p);
}

TEST(test_missing_value_short) {
    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "prog", "desc") == AP_OK);
    ASSERT(ap_add_string(p, "output", 'o', "Output", NULL) == AP_OK);

    char* argv[] = {"prog", "-o"};
    ASSERT(ap_parse(p, 2, argv) == AP_ERR_MISSING_VAL);
    ap_free(p);
}

TEST(test_int_parse_error) {
    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "prog", "desc") == AP_OK);
    ASSERT(ap_add_int(p, "count", 'n', "Count", 0) == AP_OK);

    char* argv[] = {"prog", "--count", "notanumber"};
    ASSERT(ap_parse(p, 3, argv) == AP_ERR_PARSE);
    ap_free(p);
}

TEST(test_float_parse_error) {
    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "prog", "desc") == AP_OK);
    ASSERT(ap_add_float(p, "rate", 'r', "Rate", 0.0) == AP_OK);

    char* argv[] = {"prog", "--rate", "abc"};
    ASSERT(ap_parse(p, 3, argv) == AP_ERR_PARSE);
    ap_free(p);
}

TEST(test_version_str) {
    const char* v = ap_version_str();
    ASSERT(v != NULL);
    ASSERT_EQ_STR(v, "0.1.0");
}

TEST(test_status_str) {
    ASSERT_EQ_STR(ap_status_str(AP_OK), "ok");
    ASSERT_EQ_STR(ap_status_str(AP_ERR_NULL_ARG), "null argument");
    ASSERT_EQ_STR(ap_status_str(AP_ERR_UNKNOWN_OPT), "unknown option");
    ASSERT_EQ_STR(ap_status_str(AP_ERR_MISSING_VAL), "missing value");
    ASSERT_EQ_STR(ap_status_str(AP_ERR_PARSE), "parse error");
    ASSERT_EQ_STR(ap_status_str(AP_ERR_REQUIRED), "required option missing");
}

TEST(test_null_args) {
    ASSERT(ap_create(NULL, "prog", "desc") == AP_ERR_NULL_ARG);

    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "prog", "desc") == AP_OK);
    ASSERT(ap_add_string(NULL, "x", 'x', "h", NULL) == AP_ERR_NULL_ARG);
    ASSERT(ap_add_string(p, NULL, 'x', "h", NULL) == AP_ERR_NULL_ARG);
    ASSERT(ap_add_int(NULL, "x", 'x', "h", 0) == AP_ERR_NULL_ARG);
    ASSERT(ap_add_float(NULL, "x", 'x', "h", 0.0) == AP_ERR_NULL_ARG);
    ASSERT(ap_add_bool(NULL, "x", 'x', "h") == AP_ERR_NULL_ARG);
    ASSERT(ap_add_positional(NULL, "x", "h") == AP_ERR_NULL_ARG);
    ASSERT(ap_required(NULL, "x") == AP_ERR_NULL_ARG);
    ASSERT(ap_parse(NULL, 0, NULL) == AP_ERR_NULL_ARG);
    ap_free(p);
}

TEST(test_mixed_options_and_positionals) {
    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "prog", "desc") == AP_OK);
    ASSERT(ap_add_string(p, "output", 'o', "Output", NULL) == AP_OK);
    ASSERT(ap_add_bool(p, "verbose", 'v', "Verbose") == AP_OK);
    ASSERT(ap_add_positional(p, "input", "Input file") == AP_OK);

    char* argv[] = {"prog", "-v", "input.txt", "-o", "out.bin"};
    ASSERT(ap_parse(p, 5, argv) == AP_OK);
    ASSERT(ap_get_bool(p, "verbose") == true);
    ASSERT_EQ_STR(ap_get_string(p, "output"), "out.bin");
    ASSERT_EQ_INT(ap_positional_count(p), 1);
    ASSERT_EQ_STR(ap_get_positional(p, 0), "input.txt");
    ap_free(p);
}

TEST(test_stacked_bool_then_value) {
    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "prog", "desc") == AP_OK);
    ASSERT(ap_add_bool(p, "all", 'a', "All") == AP_OK);
    ASSERT(ap_add_bool(p, "brief", 'b', "Brief") == AP_OK);
    ASSERT(ap_add_string(p, "output", 'o', "Output", NULL) == AP_OK);

    // -abo should set a, b as bool flags, then o consumes next arg as value
    char* argv[] = {"prog", "-abo", "file.txt"};
    ASSERT(ap_parse(p, 3, argv) == AP_OK);
    ASSERT(ap_get_bool(p, "all") == true);
    ASSERT(ap_get_bool(p, "brief") == true);
    ASSERT_EQ_STR(ap_get_string(p, "output"), "file.txt");
    ap_free(p);
}

TEST(test_positional_out_of_range) {
    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "prog", "desc") == AP_OK);

    char* argv[] = {"prog"};
    ASSERT(ap_parse(p, 1, argv) == AP_OK);
    ASSERT(ap_get_positional(p, 0) == NULL);
    ASSERT(ap_get_positional(p, -1) == NULL);
    ASSERT_EQ_INT(ap_positional_count(p), 0);
    ap_free(p);
}

TEST(test_usage_output) {
    ap_parser* p = NULL;
    ASSERT(ap_create(&p, "mytool", "desc") == AP_OK);
    ASSERT(ap_add_positional(p, "input", "Input") == AP_OK);

    FILE* f = tmpfile();
    ASSERT(f != NULL);
    ap_print_usage(p, f);
    long len = ftell(f);
    ASSERT(len > 0);
    fclose(f);
    ap_free(p);
}

// ── Main ────────────────────────────────────────────────────────────────────

int main(void) {
    printf("argparse.h v%s — tests\n\n", ap_version_str());

    RUN(test_string_option);
    RUN(test_int_option);
    RUN(test_float_option);
    RUN(test_bool_option);
    RUN(test_bool_not_set);
    RUN(test_short_options);
    RUN(test_short_option_attached_value);
    RUN(test_key_equals_value);
    RUN(test_positional_args);
    RUN(test_required_missing);
    RUN(test_required_provided);
    RUN(test_unknown_option);
    RUN(test_unknown_short_option);
    RUN(test_default_values);
    RUN(test_stacked_short_booleans);
    RUN(test_double_dash_separator);
    RUN(test_help_output);
    RUN(test_is_set);
    RUN(test_missing_value_long);
    RUN(test_missing_value_short);
    RUN(test_int_parse_error);
    RUN(test_float_parse_error);
    RUN(test_version_str);
    RUN(test_status_str);
    RUN(test_null_args);
    RUN(test_mixed_options_and_positionals);
    RUN(test_stacked_bool_then_value);
    RUN(test_positional_out_of_range);
    RUN(test_usage_output);

    printf("\n  %d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
