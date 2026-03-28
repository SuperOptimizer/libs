/* Kernel pattern: hex dump utility */
#include <linux/types.h>
#include <linux/kernel.h>

static const char hex_chars[] = "0123456789abcdef";

static void format_hex_byte(char *out, u8 byte)
{
    out[0] = hex_chars[byte >> 4];
    out[1] = hex_chars[byte & 0xf];
}

struct hex_dump_params {
    const void *data;
    size_t length;
    int bytes_per_line;
    int show_ascii;
    unsigned long base_addr;
};

static int hex_dump_line(char *buf, size_t buf_size,
                         const struct hex_dump_params *params,
                         size_t offset)
{
    const u8 *data = (const u8 *)params->data + offset;
    int remaining = (int)(params->length - offset);
    int line_bytes;
    int pos = 0;
    int i;

    if (remaining <= 0)
        return 0;

    line_bytes = remaining < params->bytes_per_line ?
                 remaining : params->bytes_per_line;

    /* Address */
    {
        unsigned long addr = params->base_addr + offset;
        int j;
        for (j = 60; j >= 0; j -= 4) {
            if (pos < (int)buf_size - 1)
                buf[pos++] = hex_chars[(addr >> j) & 0xf];
        }
    }
    if (pos < (int)buf_size - 1) buf[pos++] = ':';
    if (pos < (int)buf_size - 1) buf[pos++] = ' ';

    /* Hex bytes */
    for (i = 0; i < params->bytes_per_line; i++) {
        if (i < line_bytes) {
            format_hex_byte(buf + pos, data[i]);
            pos += 2;
        } else {
            if (pos < (int)buf_size - 1) buf[pos++] = ' ';
            if (pos < (int)buf_size - 1) buf[pos++] = ' ';
        }
        if (pos < (int)buf_size - 1) buf[pos++] = ' ';
    }

    /* ASCII */
    if (params->show_ascii) {
        if (pos < (int)buf_size - 1) buf[pos++] = '|';
        for (i = 0; i < line_bytes; i++) {
            char c = (data[i] >= 0x20 && data[i] < 0x7f) ? (char)data[i] : '.';
            if (pos < (int)buf_size - 1)
                buf[pos++] = c;
        }
        if (pos < (int)buf_size - 1) buf[pos++] = '|';
    }

    if (pos < (int)buf_size)
        buf[pos] = '\0';

    return pos;
}

void test_hex_dump(void)
{
    const char test_data[] = "Hello, kernel world! This is a hex dump test.";
    struct hex_dump_params params;
    char line_buf[256];
    size_t offset;
    int len;

    params.data = test_data;
    params.length = sizeof(test_data) - 1;
    params.bytes_per_line = 16;
    params.show_ascii = 1;
    params.base_addr = 0xFFFF000000000000UL;

    for (offset = 0; offset < params.length;
         offset += (size_t)params.bytes_per_line) {
        len = hex_dump_line(line_buf, sizeof(line_buf), &params, offset);
        (void)len;
    }
}
