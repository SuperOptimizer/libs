/* Kernel pattern: seq_file-like sequential output */
#include <linux/types.h>
#include <linux/kernel.h>

struct seq_output {
    char *buf;
    size_t size;
    size_t count;
    int overflow;
};

static void seq_init(struct seq_output *s, char *buf, size_t size)
{
    s->buf = buf;
    s->size = size;
    s->count = 0;
    s->overflow = 0;
}

static void seq_putc(struct seq_output *s, char c)
{
    if (s->count < s->size - 1)
        s->buf[s->count] = c;
    else
        s->overflow = 1;
    s->count++;
}

static void seq_puts(struct seq_output *s, const char *str)
{
    while (*str)
        seq_putc(s, *str++);
}

static void seq_put_decimal(struct seq_output *s, long val)
{
    char tmp[20];
    int i = 0;
    int negative = 0;
    unsigned long uval;

    if (val < 0) {
        negative = 1;
        uval = (unsigned long)(-val);
    } else {
        uval = (unsigned long)val;
    }

    if (uval == 0) {
        seq_putc(s, '0');
        return;
    }

    while (uval > 0) {
        tmp[i++] = '0' + (char)(uval % 10);
        uval /= 10;
    }

    if (negative)
        seq_putc(s, '-');

    while (i > 0)
        seq_putc(s, tmp[--i]);
}

static void seq_put_hex(struct seq_output *s, unsigned long val)
{
    static const char hex_digits[] = "0123456789abcdef";
    char tmp[16];
    int i = 0;

    if (val == 0) {
        seq_puts(s, "0x0");
        return;
    }

    while (val > 0) {
        tmp[i++] = hex_digits[val & 0xf];
        val >>= 4;
    }

    seq_puts(s, "0x");
    while (i > 0)
        seq_putc(s, tmp[--i]);
}

static void seq_terminate(struct seq_output *s)
{
    if (s->count < s->size)
        s->buf[s->count] = '\0';
    else if (s->size > 0)
        s->buf[s->size - 1] = '\0';
}

void test_seq_file(void)
{
    char buffer[256];
    struct seq_output seq;

    seq_init(&seq, buffer, sizeof(buffer));
    seq_puts(&seq, "count=");
    seq_put_decimal(&seq, 42);
    seq_puts(&seq, " addr=");
    seq_put_hex(&seq, 0xDEADBEEFUL);
    seq_puts(&seq, " negative=");
    seq_put_decimal(&seq, -123);
    seq_putc(&seq, '\n');
    seq_terminate(&seq);
}
