/* EXPECTED: 0 */
/*
 * Mock kernel printk - more realistic version closer to actual kernel patterns.
 * Exercises: enum log levels, console registration, per-CPU log buffers,
 * printk_ratelimit, pr_* macros, structured logging, format specifiers.
 */
#include <linux/stdarg.h>

#define NULL ((void *)0)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define likely(x)   __builtin_expect(!!(x), 1)

typedef unsigned long size_t;
typedef long ssize_t;

/* ---- String utilities ---- */
static size_t kstrlen(const char *s) {
    size_t n;
    n = 0;
    while (s[n]) n++;
    return n;
}

static int kstrcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a - *b;
}

static void *kmemcpy(void *dst, const void *src, size_t n) {
    char *d;
    const char *s;
    size_t i;
    d = (char *)dst;
    s = (const char *)src;
    for (i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static void *kmemset(void *s, int c, size_t n) {
    char *p;
    size_t i;
    p = (char *)s;
    for (i = 0; i < n; i++) p[i] = (char)c;
    return s;
}

/* ---- Log levels (kernel-style enum) ---- */
enum log_level {
    LOGLEVEL_EMERG = 0,
    LOGLEVEL_ALERT,
    LOGLEVEL_CRIT,
    LOGLEVEL_ERR,
    LOGLEVEL_WARNING,
    LOGLEVEL_NOTICE,
    LOGLEVEL_INFO,
    LOGLEVEL_DEBUG,
    LOGLEVEL_DEFAULT = 4
};

/* ---- Kernel log record (structured logging) ---- */
struct printk_log {
    unsigned long long ts_nsec;    /* timestamp */
    unsigned short len;            /* total record length */
    unsigned short text_len;       /* text body length */
    unsigned short dict_len;       /* dictionary length */
    unsigned char facility;        /* syslog facility */
    unsigned char flags;           /* internal flags */
    unsigned char level;           /* syslog level */
};

/* ---- Ring buffer ---- */
#define LOG_BUF_SHIFT 12
#define LOG_BUF_LEN (1 << LOG_BUF_SHIFT)

static char log_buf[LOG_BUF_LEN];
static unsigned int log_first_seq;
static unsigned int log_next_seq;
static int log_first_idx;
static int log_next_idx;

/* Spinlock mock */
typedef struct { volatile int locked; } raw_spinlock_t;

static void raw_spin_lock(raw_spinlock_t *lock) {
    lock->locked = 1;
}
static void raw_spin_unlock(raw_spinlock_t *lock) {
    lock->locked = 0;
}

static raw_spinlock_t logbuf_lock = { 0 };

/* ---- Console subsystem ---- */
struct console {
    const char *name;
    void (*write)(struct console *, const char *, unsigned int);
    int  (*setup)(struct console *, char *);
    short flags;
    short index;
    struct console *next;
};

#define CON_PRINTBUFFER  (1 << 0)
#define CON_CONSDEV      (1 << 1)
#define CON_ENABLED      (1 << 2)
#define CON_BOOT         (1 << 3)

static struct console *console_drivers;

static int register_console(struct console *newcon) {
    newcon->next = console_drivers;
    console_drivers = newcon;
    return 0;
}

/* ---- Rate limiting ---- */
struct ratelimit_state {
    int interval;    /* in jiffies */
    int burst;
    int printed;
    int missed;
    unsigned long begin;
};

#define RATELIMIT_STATE_INIT(name, intv, bst) { (intv), (bst), 0, 0, 0 }

static int __ratelimit(struct ratelimit_state *rs) {
    /* Simplified: always allow first 'burst' messages */
    if (rs->printed < rs->burst) {
        rs->printed++;
        return 1;
    }
    rs->missed++;
    return 0;
}

/* ---- Simple integer to decimal ---- */
static int int_to_str(char *buf, int buflen, long val) {
    int neg, i, len;
    char tmp[20];

    neg = 0;
    if (val < 0) { neg = 1; val = -val; }
    i = 0;
    do {
        tmp[i++] = '0' + (int)(val % 10);
        val = val / 10;
    } while (val > 0);
    if (neg) tmp[i++] = '-';
    len = i;
    if (len >= buflen) len = buflen - 1;
    i = 0;
    while (len > 0 && i < buflen - 1) {
        len--;
        buf[i++] = tmp[len];
    }
    buf[i] = '\0';
    return i;
}

/* ---- Unsigned hex ---- */
static int hex_to_str(char *buf, int buflen, unsigned long val) {
    int i, len, shift;
    char tmp[17];
    const char *hex;

    hex = "0123456789abcdef";
    i = 0;
    do {
        tmp[i++] = hex[val & 0xf];
        val >>= 4;
    } while (val > 0);
    len = i;
    if (len >= buflen) len = buflen - 1;
    shift = 0;
    while (len > 0 && shift < buflen - 1) {
        len--;
        buf[shift++] = tmp[len];
    }
    buf[shift] = '\0';
    return shift;
}

/* ---- Core printk formatting ---- */
static int vprintk_emit(int facility, int level,
                         const char *fmt, va_list args) {
    char textbuf[256];
    int textlen;
    const char *p;
    char *out;
    int remain;

    out = textbuf;
    remain = (int)sizeof(textbuf) - 1;
    textlen = 0;

    /* Add log level prefix */
    if (level >= 0 && level <= 7) {
        out[0] = '<';
        out[1] = '0' + level;
        out[2] = '>';
        out += 3;
        remain -= 3;
        textlen += 3;
    }

    for (p = fmt; *p && remain > 0; p++) {
        if (*p == '%' && *(p + 1)) {
            p++;
            if (*p == 'd' || *p == 'i') {
                int n;
                int val;
                val = va_arg(args, int);
                n = int_to_str(out, remain, (long)val);
                out += n;
                remain -= n;
                textlen += n;
            } else if (*p == 'l' && *(p + 1) == 'd') {
                long val;
                int n;
                p++;
                val = va_arg(args, long);
                n = int_to_str(out, remain, val);
                out += n;
                remain -= n;
                textlen += n;
            } else if (*p == 'u') {
                unsigned int val;
                int n;
                val = va_arg(args, unsigned int);
                n = int_to_str(out, remain, (long)val);
                out += n;
                remain -= n;
                textlen += n;
            } else if (*p == 'x' || *p == 'X') {
                unsigned long val;
                int n;
                val = (unsigned long)va_arg(args, unsigned int);
                n = hex_to_str(out, remain, val);
                out += n;
                remain -= n;
                textlen += n;
            } else if (*p == 'l' && *(p + 1) == 'x') {
                unsigned long val;
                int n;
                p++;
                val = va_arg(args, unsigned long);
                n = hex_to_str(out, remain, val);
                out += n;
                remain -= n;
                textlen += n;
            } else if (*p == 'p') {
                unsigned long val;
                int n;
                val = (unsigned long)va_arg(args, void *);
                if (remain >= 2) {
                    *out++ = '0'; *out++ = 'x';
                    remain -= 2; textlen += 2;
                }
                n = hex_to_str(out, remain, val);
                out += n;
                remain -= n;
                textlen += n;
            } else if (*p == 's') {
                const char *s;
                int slen;
                s = va_arg(args, const char *);
                if (!s) s = "(null)";
                slen = (int)kstrlen(s);
                if (slen > remain) slen = remain;
                kmemcpy(out, s, (size_t)slen);
                out += slen;
                remain -= slen;
                textlen += slen;
            } else if (*p == 'c') {
                int ch;
                ch = va_arg(args, int);
                *out++ = (char)ch;
                remain--;
                textlen++;
            } else if (*p == '%') {
                *out++ = '%';
                remain--;
                textlen++;
            }
        } else {
            *out++ = *p;
            remain--;
            textlen++;
        }
    }
    *out = '\0';

    /* Store in ring buffer */
    raw_spin_lock(&logbuf_lock);
    {
        int copy_len;
        copy_len = textlen;
        if (log_next_idx + copy_len > LOG_BUF_LEN)
            copy_len = LOG_BUF_LEN - log_next_idx;
        kmemcpy(&log_buf[log_next_idx], textbuf, (size_t)copy_len);
        log_next_idx += copy_len;
        log_next_seq++;
    }
    raw_spin_unlock(&logbuf_lock);

    /* Emit to all registered consoles */
    {
        struct console *con;
        con = console_drivers;
        while (con) {
            if (con->flags & CON_ENABLED) {
                /* This exercises the function pointer call codegen */
                con->write(con, textbuf, (unsigned int)textlen);
            }
            con = con->next;
        }
    }

    (void)facility;
    return textlen;
}

/* ---- Public printk API ---- */
static int printk(const char *fmt, ...) {
    va_list args;
    int r;
    int level;

    /* Extract log level from format string */
    level = LOGLEVEL_DEFAULT;
    if (fmt[0] == '<' && fmt[1] >= '0' && fmt[1] <= '7' && fmt[2] == '>') {
        level = fmt[1] - '0';
        fmt += 3;
    }

    va_start(args, fmt);
    r = vprintk_emit(0, level, fmt, args);
    va_end(args);
    return r;
}

/* pr_* convenience macros */
#define pr_emerg(fmt, ...)   printk("<0>" fmt, ##__VA_ARGS__)
#define pr_alert(fmt, ...)   printk("<1>" fmt, ##__VA_ARGS__)
#define pr_crit(fmt, ...)    printk("<2>" fmt, ##__VA_ARGS__)
#define pr_err(fmt, ...)     printk("<3>" fmt, ##__VA_ARGS__)
#define pr_warn(fmt, ...)    printk("<4>" fmt, ##__VA_ARGS__)
#define pr_notice(fmt, ...)  printk("<5>" fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...)    printk("<6>" fmt, ##__VA_ARGS__)
#define pr_debug(fmt, ...)   printk("<7>" fmt, ##__VA_ARGS__)

/* ---- Console driver for testing ---- */
static char console_output[4096];
static int console_pos;

static void test_console_write(struct console *con, const char *buf,
                                unsigned int len) {
    unsigned int i;
    (void)con;
    for (i = 0; i < len && console_pos < 4095; i++) {
        console_output[console_pos++] = buf[i];
    }
    console_output[console_pos] = '\0';
}

static struct console test_console = {
    "test",
    test_console_write,
    NULL,
    CON_PRINTBUFFER | CON_ENABLED,
    0,
    NULL
};

/* ---- Tests ---- */
int main(void) {
    struct ratelimit_state rs;
    int i;
    int allowed;

    /* Register console */
    register_console(&test_console);

    /* Basic printk */
    printk("<6>Hello %s\n", "world");
    if (console_pos <= 0) return 1;

    /* Check log level prefix in output */
    if (console_output[0] != '<') return 2;
    if (console_output[1] != '6') return 3;
    if (console_output[2] != '>') return 4;

    /* Test integer formatting */
    console_pos = 0;
    printk("<6>val=%d neg=%d\n", 12345, -42);
    /* Verify "12345" appears */
    {
        char *p;
        int found;
        found = 0;
        for (p = console_output; *p; p++) {
            if (p[0] == '1' && p[1] == '2' && p[2] == '3' &&
                p[3] == '4' && p[4] == '5') {
                found = 1;
                break;
            }
        }
        if (!found) return 5;
    }

    /* Test hex formatting */
    console_pos = 0;
    printk("<6>hex=%x\n", 0xDEAD);
    {
        char *p;
        int found;
        found = 0;
        for (p = console_output; *p; p++) {
            if (p[0] == 'd' && p[1] == 'e' && p[2] == 'a' && p[3] == 'd') {
                found = 1;
                break;
            }
        }
        if (!found) return 6;
    }

    /* Test pointer formatting */
    console_pos = 0;
    printk("<6>ptr=%p\n", (void *)0x1234);
    {
        char *p;
        int found;
        found = 0;
        for (p = console_output; *p; p++) {
            if (p[0] == '0' && p[1] == 'x') {
                found = 1;
                break;
            }
        }
        if (!found) return 7;
    }

    /* Test character formatting */
    console_pos = 0;
    printk("<6>char=%c\n", 'A');
    {
        char *p;
        int found;
        found = 0;
        for (p = console_output; *p; p++) {
            if (*p == 'A') { found = 1; break; }
        }
        if (!found) return 8;
    }

    /* Test ring buffer */
    if (log_next_seq < 4) return 9;
    if (log_next_idx <= 0) return 10;

    /* Test rate limiting */
    rs.interval = 100;
    rs.burst = 3;
    rs.printed = 0;
    rs.missed = 0;
    rs.begin = 0;

    allowed = 0;
    for (i = 0; i < 10; i++) {
        if (__ratelimit(&rs))
            allowed++;
    }
    if (allowed != 3) return 11;
    if (rs.missed != 7) return 12;

    /* Test pr_* macros (compile test mostly) */
    console_pos = 0;
    pr_info("info message %d\n", 1);
    if (console_pos <= 0) return 13;

    console_pos = 0;
    pr_err("error message %s\n", "bad");
    if (console_pos <= 0) return 14;

    /* Verify spinlock released */
    if (logbuf_lock.locked != 0) return 15;

    return 0;
}
