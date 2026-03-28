#define JS_IMPLEMENTATION
#include "json.h"

#include <inttypes.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>

#define ASSERT_OK(expr) do { \
    js_status _s = (expr); \
    if (_s != JS_OK) { \
        fprintf(stderr, "FAIL %s:%d: %s returned %s\n", \
                __FILE__, __LINE__, #expr, js_status_str(_s)); \
        return 1; \
    } \
} while(0)

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while(0)

#define ASSERT_EQ_INT(a, b) do { \
    int64_t _a = (a), _b = (b); \
    if (_a != _b) { \
        fprintf(stderr, "FAIL %s:%d: %" PRId64 " != %" PRId64 "\n", \
                __FILE__, __LINE__, _a, _b); \
        return 1; \
    } \
} while(0)

#define ASSERT_EQ_F64(a, b) do { \
    double _a = (a), _b = (b); \
    if (fabs(_a - _b) > 1e-9) { \
        fprintf(stderr, "FAIL %s:%d: %.9g != %.9g\n", \
                __FILE__, __LINE__, _a, _b); \
        return 1; \
    } \
} while(0)

#define ASSERT_EQ_STR(a, b) do { \
    const char* _a = (a); const char* _b = (b); \
    if (!_a || !_b || strcmp(_a, _b) != 0) { \
        fprintf(stderr, "FAIL %s:%d: \"%s\" != \"%s\"\n", \
                __FILE__, __LINE__, _a ? _a : "(null)", _b ? _b : "(null)"); \
        return 1; \
    } \
} while(0)

#define RUN_TEST(fn) do { \
    printf("  %-44s", #fn); \
    if (fn() == 0) printf("OK\n"); \
    else { printf("FAILED\n"); failures++; } \
} while(0)

// ── Tests ───────────────────────────────────────────────────────────────────

static int test_version(void)
{
    const char* v = js_version_str();
    ASSERT_TRUE(v != NULL);
    ASSERT_EQ_STR(v, "0.1.0");
    return 0;
}

static int test_status_str(void)
{
    ASSERT_EQ_STR(js_status_str(JS_OK), "JS_OK");
    ASSERT_EQ_STR(js_status_str(JS_ERR_PARSE), "JS_ERR_PARSE");
    ASSERT_EQ_STR(js_status_str(JS_ERR_NULL_ARG), "JS_ERR_NULL_ARG");
    return 0;
}

static int test_parse_null(void)
{
    js_value* v = NULL;
    ASSERT_OK(js_parse("null", &v));
    ASSERT_TRUE(js_is_null(v));
    ASSERT_TRUE(js_typeof(v) == JS_NULL);
    js_free(v);
    return 0;
}

static int test_parse_bool(void)
{
    js_value* v = NULL;
    ASSERT_OK(js_parse("true", &v));
    ASSERT_TRUE(js_is_bool(v));
    ASSERT_TRUE(js_get_bool(v) == true);
    js_free(v);

    ASSERT_OK(js_parse("false", &v));
    ASSERT_TRUE(js_is_bool(v));
    ASSERT_TRUE(js_get_bool(v) == false);
    js_free(v);
    return 0;
}

static int test_parse_int(void)
{
    js_value* v = NULL;
    ASSERT_OK(js_parse("42", &v));
    ASSERT_TRUE(js_is_int(v));
    ASSERT_EQ_INT(js_get_int(v), 42);
    js_free(v);

    ASSERT_OK(js_parse("-100", &v));
    ASSERT_TRUE(js_is_int(v));
    ASSERT_EQ_INT(js_get_int(v), -100);
    js_free(v);

    ASSERT_OK(js_parse("0", &v));
    ASSERT_EQ_INT(js_get_int(v), 0);
    js_free(v);
    return 0;
}

static int test_parse_float(void)
{
    js_value* v = NULL;
    ASSERT_OK(js_parse("3.14", &v));
    ASSERT_TRUE(js_is_float(v));
    ASSERT_EQ_F64(js_get_float(v), 3.14);
    js_free(v);

    ASSERT_OK(js_parse("-1.5e2", &v));
    ASSERT_TRUE(js_is_float(v));
    ASSERT_EQ_F64(js_get_float(v), -150.0);
    js_free(v);

    ASSERT_OK(js_parse("1E10", &v));
    ASSERT_TRUE(js_is_float(v));
    ASSERT_EQ_F64(js_get_float(v), 1e10);
    js_free(v);
    return 0;
}

static int test_parse_string(void)
{
    js_value* v = NULL;
    ASSERT_OK(js_parse("\"hello\"", &v));
    ASSERT_TRUE(js_is_string(v));
    ASSERT_EQ_STR(js_get_string(v), "hello");
    js_free(v);

    ASSERT_OK(js_parse("\"\"", &v));
    ASSERT_EQ_STR(js_get_string(v), "");
    js_free(v);
    return 0;
}

static int test_parse_escape_sequences(void)
{
    js_value* v = NULL;
    ASSERT_OK(js_parse("\"hello\\nworld\"", &v));
    ASSERT_EQ_STR(js_get_string(v), "hello\nworld");
    js_free(v);

    ASSERT_OK(js_parse("\"tab\\there\"", &v));
    ASSERT_EQ_STR(js_get_string(v), "tab\there");
    js_free(v);

    ASSERT_OK(js_parse("\"back\\\\slash\"", &v));
    ASSERT_EQ_STR(js_get_string(v), "back\\slash");
    js_free(v);

    ASSERT_OK(js_parse("\"quote\\\"here\"", &v));
    ASSERT_EQ_STR(js_get_string(v), "quote\"here");
    js_free(v);

    ASSERT_OK(js_parse("\"slash\\/ok\"", &v));
    ASSERT_EQ_STR(js_get_string(v), "slash/ok");
    js_free(v);

    // \uXXXX — ASCII range
    ASSERT_OK(js_parse("\"\\u0041\"", &v));
    ASSERT_EQ_STR(js_get_string(v), "A");
    js_free(v);

    // \uXXXX — multi-byte (Euro sign U+20AC)
    ASSERT_OK(js_parse("\"\\u20AC\"", &v));
    const char* s = js_get_string(v);
    ASSERT_TRUE(s != NULL);
    ASSERT_TRUE((unsigned char)s[0] == 0xE2);
    ASSERT_TRUE((unsigned char)s[1] == 0x82);
    ASSERT_TRUE((unsigned char)s[2] == 0xAC);
    js_free(v);
    return 0;
}

static int test_parse_array(void)
{
    js_value* v = NULL;
    ASSERT_OK(js_parse("[1, 2, 3]", &v));
    ASSERT_TRUE(js_is_array(v));
    ASSERT_EQ_INT(js_array_len(v), 3);
    ASSERT_EQ_INT(js_get_int(js_at(v, 0)), 1);
    ASSERT_EQ_INT(js_get_int(js_at(v, 1)), 2);
    ASSERT_EQ_INT(js_get_int(js_at(v, 2)), 3);
    ASSERT_TRUE(js_at(v, 3) == NULL);
    ASSERT_TRUE(js_at(v, -1) == NULL);
    js_free(v);

    // Empty array
    ASSERT_OK(js_parse("[]", &v));
    ASSERT_TRUE(js_is_array(v));
    ASSERT_EQ_INT(js_array_len(v), 0);
    js_free(v);
    return 0;
}

static int test_parse_object(void)
{
    js_value* v = NULL;
    ASSERT_OK(js_parse("{\"name\": \"test\", \"value\": 42}", &v));
    ASSERT_TRUE(js_is_object(v));
    ASSERT_EQ_INT(js_object_len(v), 2);
    ASSERT_EQ_STR(js_get_string(js_get(v, "name")), "test");
    ASSERT_EQ_INT(js_get_int(js_get(v, "value")), 42);
    ASSERT_TRUE(js_get(v, "missing") == NULL);
    js_free(v);

    // Empty object
    ASSERT_OK(js_parse("{}", &v));
    ASSERT_TRUE(js_is_object(v));
    ASSERT_EQ_INT(js_object_len(v), 0);
    js_free(v);
    return 0;
}

static int test_parse_nested(void)
{
    const char* json =
        "{\"users\": [{\"name\": \"Alice\", \"age\": 30},"
        " {\"name\": \"Bob\", \"age\": 25}],"
        " \"count\": 2}";
    js_value* v = NULL;
    ASSERT_OK(js_parse(json, &v));

    js_value* users = js_get(v, "users");
    ASSERT_TRUE(js_is_array(users));
    ASSERT_EQ_INT(js_array_len(users), 2);

    js_value* alice = js_at(users, 0);
    ASSERT_EQ_STR(js_get_string(js_get(alice, "name")), "Alice");
    ASSERT_EQ_INT(js_get_int(js_get(alice, "age")), 30);

    js_value* bob = js_at(users, 1);
    ASSERT_EQ_STR(js_get_string(js_get(bob, "name")), "Bob");
    ASSERT_EQ_INT(js_get_int(js_get(bob, "age")), 25);

    ASSERT_EQ_INT(js_get_int(js_get(v, "count")), 2);
    js_free(v);
    return 0;
}

static int test_parse_mixed_array(void)
{
    js_value* v = NULL;
    ASSERT_OK(js_parse("[null, true, 42, 3.14, \"hello\"]", &v));
    ASSERT_TRUE(js_is_null(js_at(v, 0)));
    ASSERT_TRUE(js_get_bool(js_at(v, 1)));
    ASSERT_EQ_INT(js_get_int(js_at(v, 2)), 42);
    ASSERT_EQ_F64(js_get_float(js_at(v, 3)), 3.14);
    ASSERT_EQ_STR(js_get_string(js_at(v, 4)), "hello");
    js_free(v);
    return 0;
}

static int test_parse_whitespace(void)
{
    js_value* v = NULL;
    ASSERT_OK(js_parse("  \n\t { \n \"a\" \t : \r\n 1 \n } \t ", &v));
    ASSERT_TRUE(js_is_object(v));
    ASSERT_EQ_INT(js_get_int(js_get(v, "a")), 1);
    js_free(v);
    return 0;
}

static int test_create_values(void)
{
    js_value* v;

    v = js_null();
    ASSERT_TRUE(js_is_null(v));
    js_free(v);

    v = js_bool(true);
    ASSERT_TRUE(js_get_bool(v) == true);
    js_free(v);

    v = js_int(12345);
    ASSERT_EQ_INT(js_get_int(v), 12345);
    js_free(v);

    v = js_float(2.718);
    ASSERT_EQ_F64(js_get_float(v), 2.718);
    js_free(v);

    v = js_string("test");
    ASSERT_EQ_STR(js_get_string(v), "test");
    js_free(v);

    return 0;
}

static int test_mutation_set(void)
{
    js_value* obj = js_object();
    ASSERT_OK(js_set(obj, "x", js_int(10)));
    ASSERT_OK(js_set(obj, "y", js_string("hello")));
    ASSERT_EQ_INT(js_object_len(obj), 2);
    ASSERT_EQ_INT(js_get_int(js_get(obj, "x")), 10);
    ASSERT_EQ_STR(js_get_string(js_get(obj, "y")), "hello");

    // Overwrite existing key
    ASSERT_OK(js_set(obj, "x", js_int(99)));
    ASSERT_EQ_INT(js_get_int(js_get(obj, "x")), 99);
    ASSERT_EQ_INT(js_object_len(obj), 2); // len unchanged

    js_free(obj);
    return 0;
}

static int test_mutation_push(void)
{
    js_value* arr = js_array();
    ASSERT_OK(js_push(arr, js_int(1)));
    ASSERT_OK(js_push(arr, js_int(2)));
    ASSERT_OK(js_push(arr, js_string("three")));
    ASSERT_EQ_INT(js_array_len(arr), 3);
    ASSERT_EQ_INT(js_get_int(js_at(arr, 0)), 1);
    ASSERT_EQ_INT(js_get_int(js_at(arr, 1)), 2);
    ASSERT_EQ_STR(js_get_string(js_at(arr, 2)), "three");
    js_free(arr);
    return 0;
}

static int test_contains(void)
{
    js_value* v = NULL;
    ASSERT_OK(js_parse("{\"a\": 1, \"b\": 2}", &v));
    ASSERT_TRUE(js_contains(v, "a"));
    ASSERT_TRUE(js_contains(v, "b"));
    ASSERT_TRUE(!js_contains(v, "c"));
    ASSERT_TRUE(!js_contains(NULL, "a"));
    js_free(v);
    return 0;
}

static int test_clone(void)
{
    const char* json = "{\"arr\": [1, 2, 3], \"str\": \"hello\", \"flag\": true}";
    js_value* orig = NULL;
    ASSERT_OK(js_parse(json, &orig));

    js_value* copy = NULL;
    ASSERT_OK(js_clone(&copy, orig));

    // Verify deep copy
    ASSERT_TRUE(copy != orig);
    ASSERT_EQ_STR(js_get_string(js_get(copy, "str")), "hello");
    ASSERT_TRUE(js_get_bool(js_get(copy, "flag")));

    js_value* arr = js_get(copy, "arr");
    ASSERT_EQ_INT(js_array_len(arr), 3);
    ASSERT_EQ_INT(js_get_int(js_at(arr, 0)), 1);

    // Mutate original, copy should be unaffected
    ASSERT_OK(js_set(orig, "str", js_string("changed")));
    ASSERT_EQ_STR(js_get_string(js_get(copy, "str")), "hello");

    js_free(orig);
    js_free(copy);
    return 0;
}

static int test_dump_compact(void)
{
    js_value* v = NULL;
    ASSERT_OK(js_parse("{\"a\":1,\"b\":[2,3]}", &v));

    char* out = NULL;
    ASSERT_OK(js_dump(v, &out, 0));
    ASSERT_EQ_STR(out, "{\"a\":1,\"b\":[2,3]}");
    JS_FREE(out);
    js_free(v);
    return 0;
}

static int test_dump_pretty(void)
{
    js_value* v = NULL;
    ASSERT_OK(js_parse("{\"a\":1}", &v));

    char* out = NULL;
    ASSERT_OK(js_dump(v, &out, 2));
    // Should contain newlines and indentation
    ASSERT_TRUE(strstr(out, "\n") != NULL);
    ASSERT_TRUE(strstr(out, "  \"a\"") != NULL);
    JS_FREE(out);
    js_free(v);
    return 0;
}

static int test_roundtrip(void)
{
    const char* json = "{\"name\":\"test\",\"values\":[1,2,3],\"nested\":{\"x\":true}}";
    js_value* v = NULL;
    ASSERT_OK(js_parse(json, &v));

    char* out = NULL;
    ASSERT_OK(js_dump(v, &out, 0));

    // Re-parse the serialized output
    js_value* v2 = NULL;
    ASSERT_OK(js_parse(out, &v2));

    ASSERT_EQ_STR(js_get_string(js_get(v2, "name")), "test");
    ASSERT_EQ_INT(js_array_len(js_get(v2, "values")), 3);
    ASSERT_TRUE(js_get_bool(js_get(js_get(v2, "nested"), "x")));

    JS_FREE(out);
    js_free(v);
    js_free(v2);
    return 0;
}

static int test_file_io(void)
{
    const char* path = "/tmp/test_json_lib.json";

    // Build an object and write to file
    js_value* obj = js_object();
    ASSERT_OK(js_set(obj, "hello", js_string("world")));
    ASSERT_OK(js_set(obj, "num", js_int(42)));
    ASSERT_OK(js_dump_file(obj, path, 2));
    js_free(obj);

    // Read back
    js_value* v = NULL;
    ASSERT_OK(js_parse_file(path, &v));
    ASSERT_EQ_STR(js_get_string(js_get(v, "hello")), "world");
    ASSERT_EQ_INT(js_get_int(js_get(v, "num")), 42);
    js_free(v);

    remove(path);
    return 0;
}

static int test_object_iter(void)
{
    js_value* v = NULL;
    ASSERT_OK(js_parse("{\"a\":1,\"b\":2,\"c\":3}", &v));

    js_object_iter it = js_object_iter_init(v);
    int count = 0;
    while (js_object_next(&it)) {
        const char* key = js_object_key(&it);
        ASSERT_TRUE(key != NULL);
        js_value* val = js_object_val(&it);
        ASSERT_TRUE(js_is_int(val));
        count++;
    }
    ASSERT_EQ_INT(count, 3);
    js_free(v);
    return 0;
}

static int test_error_cases(void)
{
    js_value* v = NULL;

    // NULL args
    ASSERT_TRUE(js_parse(NULL, &v) == JS_ERR_NULL_ARG);
    ASSERT_TRUE(js_parse("null", NULL) == JS_ERR_NULL_ARG);

    // Invalid JSON
    ASSERT_TRUE(js_parse("", &v) == JS_ERR_PARSE);
    ASSERT_TRUE(js_parse("{", &v) == JS_ERR_PARSE);
    ASSERT_TRUE(js_parse("[1,]", &v) == JS_ERR_PARSE);
    ASSERT_TRUE(js_parse("nope", &v) == JS_ERR_PARSE);
    ASSERT_TRUE(js_parse("{\"a\":1} extra", &v) == JS_ERR_PARSE);

    // Type errors
    js_value* num = js_int(5);
    ASSERT_TRUE(js_set(num, "key", js_int(1)) == JS_ERR_TYPE);
    js_free(num);

    js_value* str = js_string("hi");
    ASSERT_TRUE(js_push(str, js_int(1)) == JS_ERR_TYPE);
    js_free(str);

    // Missing file
    ASSERT_TRUE(js_parse_file("/tmp/nonexistent_json_test_xxx.json", &v) == JS_ERR_IO);

    return 0;
}

static int test_type_queries(void)
{
    ASSERT_TRUE(js_is_null(NULL));
    ASSERT_TRUE(js_typeof(NULL) == JS_NULL);
    ASSERT_TRUE(js_get_bool(NULL) == false);
    ASSERT_TRUE(js_get_int(NULL) == 0);
    ASSERT_EQ_F64(js_get_float(NULL), 0.0);
    ASSERT_TRUE(js_get_string(NULL) == NULL);
    ASSERT_EQ_INT(js_array_len(NULL), 0);
    ASSERT_EQ_INT(js_object_len(NULL), 0);
    return 0;
}

static int test_float_int_coercion(void)
{
    // js_get_float should work on int values too
    js_value* v = js_int(42);
    ASSERT_EQ_F64(js_get_float(v), 42.0);
    js_free(v);
    return 0;
}

static int test_dump_primitives(void)
{
    char* out = NULL;

    js_value* v = js_null();
    ASSERT_OK(js_dump(v, &out, 0));
    ASSERT_EQ_STR(out, "null");
    JS_FREE(out); js_free(v);

    v = js_bool(true);
    ASSERT_OK(js_dump(v, &out, 0));
    ASSERT_EQ_STR(out, "true");
    JS_FREE(out); js_free(v);

    v = js_bool(false);
    ASSERT_OK(js_dump(v, &out, 0));
    ASSERT_EQ_STR(out, "false");
    JS_FREE(out); js_free(v);

    v = js_int(-99);
    ASSERT_OK(js_dump(v, &out, 0));
    ASSERT_EQ_STR(out, "-99");
    JS_FREE(out); js_free(v);

    v = js_string("a\"b\\c\n");
    ASSERT_OK(js_dump(v, &out, 0));
    ASSERT_EQ_STR(out, "\"a\\\"b\\\\c\\n\"");
    JS_FREE(out); js_free(v);

    return 0;
}

static int test_large_array(void)
{
    js_value* arr = js_array();
    for (int i = 0; i < 1000; i++) {
        ASSERT_OK(js_push(arr, js_int(i)));
    }
    ASSERT_EQ_INT(js_array_len(arr), 1000);
    ASSERT_EQ_INT(js_get_int(js_at(arr, 999)), 999);
    js_free(arr);
    return 0;
}

static int test_clone_null(void)
{
    js_value* out = NULL;
    ASSERT_OK(js_clone(&out, NULL));
    ASSERT_TRUE(out == NULL);
    return 0;
}

// ── Main ────────────────────────────────────────────────────────────────────

int main(void)
{
    int failures = 0;
    int total = 25;
    printf("json.h v%s tests:\n", js_version_str());

    RUN_TEST(test_version);
    RUN_TEST(test_status_str);
    RUN_TEST(test_parse_null);
    RUN_TEST(test_parse_bool);
    RUN_TEST(test_parse_int);
    RUN_TEST(test_parse_float);
    RUN_TEST(test_parse_string);
    RUN_TEST(test_parse_escape_sequences);
    RUN_TEST(test_parse_array);
    RUN_TEST(test_parse_object);
    RUN_TEST(test_parse_nested);
    RUN_TEST(test_parse_mixed_array);
    RUN_TEST(test_parse_whitespace);
    RUN_TEST(test_create_values);
    RUN_TEST(test_mutation_set);
    RUN_TEST(test_mutation_push);
    RUN_TEST(test_contains);
    RUN_TEST(test_clone);
    RUN_TEST(test_dump_compact);
    RUN_TEST(test_dump_pretty);
    RUN_TEST(test_roundtrip);
    RUN_TEST(test_file_io);
    RUN_TEST(test_object_iter);
    RUN_TEST(test_error_cases);
    RUN_TEST(test_type_queries);
    RUN_TEST(test_float_int_coercion);
    RUN_TEST(test_dump_primitives);
    RUN_TEST(test_large_array);
    RUN_TEST(test_clone_null);

    total = 29;
    printf("\n%d/%d tests passed\n", total - failures, total);
    return failures ? 1 : 0;
}
