/* Kernel pattern: character type operations */
#include <linux/types.h>
#include <linux/ctype.h>
#include <linux/kernel.h>

static int parse_hex_digit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static unsigned long parse_hex_string(const char *s)
{
    unsigned long result = 0;
    int digit;

    /* Skip 0x prefix */
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;

    while (*s) {
        digit = parse_hex_digit(*s);
        if (digit < 0)
            break;
        result = (result << 4) | (unsigned long)digit;
        s++;
    }
    return result;
}

static int parse_decimal(const char *s, long *result)
{
    int negative = 0;
    long val = 0;

    while (*s == ' ' || *s == '\t')
        s++;

    if (*s == '-') {
        negative = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    if (!(*s >= '0' && *s <= '9'))
        return -1;

    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }

    *result = negative ? -val : val;
    return 0;
}

static int is_valid_identifier(const char *s)
{
    if (!s || !*s)
        return 0;
    if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || *s == '_'))
        return 0;
    s++;
    while (*s) {
        if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
              (*s >= '0' && *s <= '9') || *s == '_'))
            return 0;
        s++;
    }
    return 1;
}

void test_ctype(void)
{
    unsigned long hex;
    long dec;
    int valid;

    hex = parse_hex_string("0xDEADBEEF");
    (void)hex;

    parse_decimal("-12345", &dec);
    (void)dec;

    valid = is_valid_identifier("my_variable_123");
    (void)valid;

    valid = is_valid_identifier("123invalid");
    (void)valid;
}
