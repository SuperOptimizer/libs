/* Kernel pattern: string operations */
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/slab.h>

static int my_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *(unsigned char *)a - *(unsigned char *)b;
}

static char *my_strdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *dup = kmalloc(len, GFP_KERNEL);
    if (dup)
        memcpy(dup, s, len);
    return dup;
}

static void str_to_upper(char *s)
{
    while (*s) {
        if (*s >= 'a' && *s <= 'z')
            *s -= 32;
        s++;
    }
}

static void str_to_lower(char *s)
{
    while (*s) {
        if (*s >= 'A' && *s <= 'Z')
            *s += 32;
        s++;
    }
}

static int str_count_char(const char *s, char c)
{
    int count = 0;
    while (*s) {
        if (*s == c)
            count++;
        s++;
    }
    return count;
}

static const char *str_skip_spaces(const char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\n')
        s++;
    return s;
}

static char *str_trim_right(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\n'))
        s[--len] = '\0';
    return s;
}

void test_string_ops(void)
{
    char buf[128];
    char *dup;
    const char *p;
    int cnt;
    int cmp;

    memset(buf, 0, sizeof(buf));
    strcpy(buf, "Hello, Kernel World!");

    cmp = my_strcmp("abc", "abd");
    (void)cmp;

    dup = my_strdup(buf);
    if (dup) {
        str_to_upper(dup);
        str_to_lower(dup);
        kfree(dup);
    }

    cnt = str_count_char(buf, 'l');
    (void)cnt;

    p = str_skip_spaces("   hello");
    (void)p;

    strcpy(buf, "trailing spaces   ");
    str_trim_right(buf);
}
