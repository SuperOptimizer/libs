/* EXPECTED: 0 */
/* Mock kernel printk patterns - variadic functions, spinlocks, ring buffers */
#include <linux/stdarg.h>

#define __printf(a, b) __attribute__((format(printf, a, b)))
#define noinline __attribute__((noinline))
#define unlikely(x) __builtin_expect(!!(x), 0)
#define likely(x) __builtin_expect(!!(x), 1)

/* Log levels */
#define KERN_EMERG   "<0>"
#define KERN_ALERT   "<1>"
#define KERN_ERR     "<3>"
#define KERN_WARNING "<4>"
#define KERN_INFO    "<6>"
#define KERN_DEBUG   "<7>"

/* Spinlock mock */
typedef struct { volatile int locked; } raw_spinlock_t;
#define __RAW_SPIN_LOCK_INITIALIZER { 0 }

static void raw_spin_lock(raw_spinlock_t *lock) {
    lock->locked = 1;
}
static void raw_spin_unlock(raw_spinlock_t *lock) {
    lock->locked = 0;
}

/* Ring buffer mock */
#define LOG_BUF_LEN 4096
static char log_buf[LOG_BUF_LEN];
static int log_end;
static raw_spinlock_t logbuf_lock = __RAW_SPIN_LOCK_INITIALIZER;

/* printk with inline formatting - va_arg used directly */
int __printf(1, 2) printk(const char *fmt, ...) {
    va_list args;
    const char *p;
    int written;
    int val;
    char numbuf[20];
    int ni, neg;
    const char *s;

    va_start(args, fmt);
    raw_spin_lock(&logbuf_lock);

    written = 0;
    for (p = fmt; *p && log_end < LOG_BUF_LEN - 1; p++) {
        if (*p == '%' && *(p + 1)) {
            p++;
            if (*p == 'd') {
                val = va_arg(args, int);
                neg = 0;
                if (val < 0) { neg = 1; val = -val; }
                ni = 0;
                do {
                    numbuf[ni++] = '0' + (val % 10);
                    val = val / 10;
                } while (val > 0);
                if (neg) numbuf[ni++] = '-';
                while (ni > 0 && log_end < LOG_BUF_LEN - 1) {
                    ni--;
                    log_buf[log_end++] = numbuf[ni];
                    written++;
                }
            } else if (*p == 's') {
                s = va_arg(args, const char *);
                if (!s) s = "(null)";
                while (*s && log_end < LOG_BUF_LEN - 1) {
                    log_buf[log_end++] = *s++;
                    written++;
                }
            } else if (*p == '%') {
                log_buf[log_end++] = '%';
                written++;
            } else {
                log_buf[log_end++] = '%';
                log_buf[log_end++] = *p;
                written += 2;
            }
        } else {
            log_buf[log_end++] = *p;
            written++;
        }
    }
    log_buf[log_end] = '\0';

    raw_spin_unlock(&logbuf_lock);
    va_end(args);
    return written;
}

/* Log level extraction - kernel pattern */
static int log_level(const char *msg) {
    if (msg[0] == '<' && msg[1] >= '0' && msg[1] <= '7' && msg[2] == '>') {
        return msg[1] - '0';
    }
    return 4;
}

/* Test it */
int main(void) {
    int level;
    int buf_used;
    int r1, r2, r3;

    r1 = printk(KERN_INFO "Hello %s, value=%d\n", "world", 42);
    r2 = printk(KERN_DEBUG "Log at %d\n", log_end);
    r3 = printk(KERN_ERR "Error code=%d msg=%s\n", -1, "failure");

    /* Check something was written */
    if (r1 <= 0) return 1;
    if (r2 <= 0) return 2;
    if (r3 <= 0) return 3;

    /* Check log level extraction */
    level = log_level(log_buf);
    if (level != 6) return 4;

    /* Check buffer has content */
    buf_used = log_end;
    if (buf_used <= 0) return 5;

    /* Verify spinlock is released */
    if (logbuf_lock.locked != 0) return 6;

    return 0;
}
