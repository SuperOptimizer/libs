/* Stub seq_buf.h for free-cc kernel compilation testing */
#ifndef _LINUX_SEQ_BUF_H
#define _LINUX_SEQ_BUF_H

#include <linux/types.h>

struct seq_buf {
    char *buffer;
    size_t size;
    size_t len;
};

extern int seq_buf_printf(struct seq_buf *s, const char *fmt, ...);
extern int seq_buf_putc(struct seq_buf *s, unsigned char c);
extern int seq_buf_puts(struct seq_buf *s, const char *str);

#endif
