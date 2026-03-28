/*
 * diag.c - Diagnostic infrastructure for the free C compiler.
 * Provides warning categories, -Wall/-Werror/-w flag support,
 * and error recovery helpers.
 * Pure C89. No external dependencies beyond system libc.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "free.h"

/* ---- global diagnostic state ---- */
unsigned int cc_warnings;          /* bitmask of enabled warnings */
int cc_werror;                     /* -Werror: treat warnings as errors */
int cc_suppress_warnings;          /* -w: suppress all warnings */
int cc_error_count;
int cc_warning_count;
int cc_error_limit = 20;           /* -ferror-limit=N */

/* ---- recovery state ---- */
int cc_in_recovery;                /* 1 if currently recovering from error */

/* ---- warning flag table ---- */
struct warn_flag {
    const char *name;
    unsigned int bit;
};

static const struct warn_flag warn_flags[] = {
    { "unused-variable",   W_UNUSED_VAR },
    { "unused-function",   W_UNUSED_FUNC },
    { "unused-parameter",  W_UNUSED_PARAM },
    { "implicit-function-declaration", W_IMPLICIT_FUNC },
    { "return-type",       W_RETURN_TYPE },
    { "format",            W_FORMAT },
    { "shadow",            W_SHADOW },
    { "sign-compare",      W_SIGN_COMPARE },
    { NULL, 0 }
};

/* ---- diagnostic output ---- */

void diag_warn(const char *file, int line, int col, const char *fmt, ...)
{
    va_list ap;

    if (cc_suppress_warnings) {
        return;
    }

    if (cc_werror) {
        /* treat as error */
        va_start(ap, fmt);
        fprintf(stderr, "%s:%d:%d: error: ", file, line, col);
        vfprintf(stderr, fmt, ap);
        fprintf(stderr, " [-Werror]\n");
        va_end(ap);
        cc_error_count++;
        if (cc_error_count >= cc_error_limit) {
            fprintf(stderr, "free-cc: too many errors (%d), stopping\n",
                    cc_error_count);
            exit(1);
        }
        return;
    }

    va_start(ap, fmt);
    fprintf(stderr, "%s:%d:%d: warning: ", file, line, col);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    cc_warning_count++;
}

void diag_error(const char *file, int line, int col, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(stderr, "%s:%d:%d: error: ", file, line, col);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    cc_error_count++;

    if (cc_error_count >= cc_error_limit) {
        fprintf(stderr, "free-cc: too many errors (%d), stopping\n",
                cc_error_count);
        exit(1);
    }
}

/* ---- warning flag parsing ---- */

void diag_init(void)
{
    cc_warnings = 0;
    cc_werror = 0;
    cc_suppress_warnings = 0;
    cc_error_count = 0;
    cc_warning_count = 0;
    cc_error_limit = 20;
    cc_in_recovery = 0;
}

int diag_had_errors(void)
{
    return cc_error_count > 0;
}

/*
 * diag_parse_warning_flag - parse a -W flag from the command line.
 * Returns 1 if the flag was recognized, 0 otherwise.
 * Handles: -Wall, -Wextra, -Werror, -w, -Wno-<name>, -W<name>
 */
int diag_parse_warning_flag(const char *flag)
{
    int i;
    int negate;
    const char *name;

    if (strcmp(flag, "-Wall") == 0) {
        cc_warnings |= W_UNUSED_VAR | W_IMPLICIT_FUNC
                     | W_RETURN_TYPE | W_FORMAT;
        return 1;
    }

    if (strcmp(flag, "-Wextra") == 0) {
        cc_warnings |= W_UNUSED_PARAM | W_SIGN_COMPARE;
        return 1;
    }

    if (strcmp(flag, "-Werror") == 0) {
        cc_werror = 1;
        return 1;
    }

    if (strcmp(flag, "-w") == 0) {
        cc_suppress_warnings = 1;
        return 1;
    }

    if (strcmp(flag, "-Wshadow") == 0) {
        cc_warnings |= W_SHADOW;
        return 1;
    }

    /* -Wno-<name> and -W<name> */
    if (flag[0] == '-' && flag[1] == 'W') {
        name = flag + 2;
        negate = 0;
        if (strncmp(name, "no-", 3) == 0) {
            negate = 1;
            name += 3;
        }

        for (i = 0; warn_flags[i].name != NULL; i++) {
            if (strcmp(name, warn_flags[i].name) == 0) {
                if (negate) {
                    cc_warnings &= ~warn_flags[i].bit;
                } else {
                    cc_warnings |= warn_flags[i].bit;
                }
                return 1;
            }
        }

        /* unknown -W flag: accept silently (gcc compat) */
        return 1;
    }

    return 0;
}

/*
 * diag_parse_error_limit - parse -ferror-limit=N.
 * Returns 1 if recognized, 0 otherwise.
 */
int diag_parse_error_limit(const char *flag)
{
    if (strncmp(flag, "-ferror-limit=", 14) == 0) {
        cc_error_limit = atoi(flag + 14);
        if (cc_error_limit <= 0) {
            cc_error_limit = 20;
        }
        return 1;
    }
    return 0;
}

int diag_warn_enabled(unsigned int category)
{
    if (cc_suppress_warnings) {
        return 0;
    }
    return (cc_warnings & category) != 0;
}
