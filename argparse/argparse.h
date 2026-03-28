// argparse.h — Pure C23 command-line argument parsing library
// Single-header: declare API, then #define AP_IMPLEMENTATION in one .c file.
// Vesuvius Challenge / Villa Volume Cartographer.
#ifndef ARGPARSE_H
#define ARGPARSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ── Version ─────────────────────────────────────────────────────────────────

#define AP_VERSION_MAJOR 0
#define AP_VERSION_MINOR 1
#define AP_VERSION_PATCH 0

// ── C23 Compat ──────────────────────────────────────────────────────────────

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
  #define AP_NODISCARD    [[nodiscard]]
  #define AP_MAYBE_UNUSED [[maybe_unused]]
#elif defined(__GNUC__) || defined(__clang__)
  #define AP_NODISCARD    __attribute__((warn_unused_result))
  #define AP_MAYBE_UNUSED __attribute__((unused))
#else
  #define AP_NODISCARD
  #define AP_MAYBE_UNUSED
#endif

// ── Linkage ─────────────────────────────────────────────────────────────────

#ifndef APDEF
  #ifdef AP_STATIC
    #define APDEF static
  #else
    #define APDEF extern
  #endif
#endif

// ── Allocator Hooks ─────────────────────────────────────────────────────────

#ifndef AP_MALLOC
  #include <stdlib.h>
  #define AP_MALLOC(sz)     malloc(sz)
  #define AP_FREE(p)        free(p)
  #define AP_CALLOC(n, sz)  calloc(n, sz)
#endif

// ── Constants ───────────────────────────────────────────────────────────────

#define AP_MAX_OPTIONS     64
#define AP_MAX_POSITIONALS 64

// ── Status Codes ────────────────────────────────────────────────────────────

typedef enum ap_status {
    AP_OK = 0,
    AP_ERR_NULL_ARG,
    AP_ERR_UNKNOWN_OPT,
    AP_ERR_MISSING_VAL,
    AP_ERR_PARSE,
    AP_ERR_REQUIRED,
} ap_status;

// ── Option Types ────────────────────────────────────────────────────────────

typedef enum ap_opt_type {
    AP_OPT_STRING = 0,
    AP_OPT_INT,
    AP_OPT_FLOAT,
    AP_OPT_BOOL,
} ap_opt_type;

// ── Internal Option Struct ──────────────────────────────────────────────────

typedef struct ap_option {
    const char* long_opt;      // long option name (without --)
    char        short_opt;     // single-char short option (0 if none)
    const char* help;          // help text
    ap_opt_type type;          // value type
    bool        required;      // is this option required?
    bool        is_set;        // was this option explicitly provided?

    // Default values
    const char* default_str;
    int64_t     default_int;
    double      default_float;

    // Parsed value (stored as string, converted on access)
    const char* value_str;     // points into argv or default_str
    int64_t     value_int;
    double      value_float;
    bool        value_bool;
} ap_option;

// ── Positional Arg ──────────────────────────────────────────────────────────

typedef struct ap_positional_def {
    const char* name;
    const char* help;
} ap_positional_def;

// ── Parser ──────────────────────────────────────────────────────────────────

typedef struct ap_parser {
    const char*      program_name;
    const char*      description;

    ap_option        options[AP_MAX_OPTIONS];
    int              option_count;

    ap_positional_def positional_defs[AP_MAX_POSITIONALS];
    int              positional_def_count;

    const char*      positionals[AP_MAX_POSITIONALS];
    int              positional_count;
} ap_parser;

// ── Utilities ───────────────────────────────────────────────────────────────

APDEF const char* ap_status_str(ap_status s);
APDEF const char* ap_version_str(void);

// ── Lifecycle ───────────────────────────────────────────────────────────────

AP_NODISCARD APDEF ap_status ap_create(ap_parser** out,
                                       const char* program_name,
                                       const char* description);
APDEF void ap_free(ap_parser* p);

// ── Adding Options ──────────────────────────────────────────────────────────

AP_NODISCARD APDEF ap_status ap_add_string(ap_parser* p, const char* long_opt,
                                           char short_opt, const char* help,
                                           const char* default_val);
AP_NODISCARD APDEF ap_status ap_add_int(ap_parser* p, const char* long_opt,
                                        char short_opt, const char* help,
                                        int64_t default_val);
AP_NODISCARD APDEF ap_status ap_add_float(ap_parser* p, const char* long_opt,
                                          char short_opt, const char* help,
                                          double default_val);
AP_NODISCARD APDEF ap_status ap_add_bool(ap_parser* p, const char* long_opt,
                                         char short_opt, const char* help);
AP_NODISCARD APDEF ap_status ap_add_positional(ap_parser* p, const char* name,
                                               const char* help);

// ── Required ────────────────────────────────────────────────────────────────

AP_NODISCARD APDEF ap_status ap_required(ap_parser* p, const char* long_opt);

// ── Parsing ─────────────────────────────────────────────────────────────────

AP_NODISCARD APDEF ap_status ap_parse(ap_parser* p, int argc, char** argv);

// ── Accessing Results ───────────────────────────────────────────────────────

APDEF const char* ap_get_string(const ap_parser* p, const char* long_opt);
APDEF int64_t     ap_get_int(const ap_parser* p, const char* long_opt);
APDEF double      ap_get_float(const ap_parser* p, const char* long_opt);
APDEF bool        ap_get_bool(const ap_parser* p, const char* long_opt);
APDEF bool        ap_is_set(const ap_parser* p, const char* long_opt);

APDEF const char* ap_get_positional(const ap_parser* p, int index);
APDEF int         ap_positional_count(const ap_parser* p);

// ── Help ────────────────────────────────────────────────────────────────────

APDEF void ap_print_help(const ap_parser* p, FILE* f);
APDEF void ap_print_usage(const ap_parser* p, FILE* f);

// ═══════════════════════════════════════════════════════════════════════════
// Implementation
// ═══════════════════════════════════════════════════════════════════════════

#ifdef AP_IMPLEMENTATION

#include <errno.h>
#include <inttypes.h>
#include <math.h>

// ── Utilities ───────────────────────────────────────────────────────────────

APDEF const char* ap_status_str(ap_status s) {
    switch (s) {
        case AP_OK:              return "ok";
        case AP_ERR_NULL_ARG:    return "null argument";
        case AP_ERR_UNKNOWN_OPT: return "unknown option";
        case AP_ERR_MISSING_VAL: return "missing value";
        case AP_ERR_PARSE:       return "parse error";
        case AP_ERR_REQUIRED:    return "required option missing";
    }
    return "unknown status";
}

APDEF const char* ap_version_str(void) {
    static char buf[32];
    snprintf(buf, sizeof(buf), "%d.%d.%d",
             AP_VERSION_MAJOR, AP_VERSION_MINOR, AP_VERSION_PATCH);
    return buf;
}

// ── Internal Helpers ────────────────────────────────────────────────────────

static ap_option* ap__find_long(const ap_parser* p, const char* name) {
    for (int i = 0; i < p->option_count; i++) {
        if (p->options[i].long_opt && strcmp(p->options[i].long_opt, name) == 0)
            return (ap_option*)&p->options[i];
    }
    return NULL;
}

static ap_option* ap__find_short(const ap_parser* p, char c) {
    for (int i = 0; i < p->option_count; i++) {
        if (p->options[i].short_opt == c)
            return (ap_option*)&p->options[i];
    }
    return NULL;
}

static ap_status ap__set_value(ap_option* opt, const char* val) {
    opt->is_set = true;
    opt->value_str = val;

    switch (opt->type) {
        case AP_OPT_STRING:
            break;
        case AP_OPT_INT: {
            char* end = NULL;
            errno = 0;
            int64_t v = strtoll(val, &end, 0);
            if (errno != 0 || end == val || *end != '\0')
                return AP_ERR_PARSE;
            opt->value_int = v;
            break;
        }
        case AP_OPT_FLOAT: {
            char* end = NULL;
            errno = 0;
            double v = strtod(val, &end);
            if (errno != 0 || end == val || *end != '\0')
                return AP_ERR_PARSE;
            opt->value_float = v;
            break;
        }
        case AP_OPT_BOOL:
            opt->value_bool = true;
            break;
    }
    return AP_OK;
}

// ── Lifecycle ───────────────────────────────────────────────────────────────

AP_NODISCARD APDEF ap_status ap_create(ap_parser** out,
                                       const char* program_name,
                                       const char* description) {
    if (!out) return AP_ERR_NULL_ARG;

    ap_parser* p = (ap_parser*)AP_CALLOC(1, sizeof(ap_parser));
    if (!p) return AP_ERR_PARSE;

    p->program_name = program_name;
    p->description  = description;
    *out = p;
    return AP_OK;
}

APDEF void ap_free(ap_parser* p) {
    if (p) AP_FREE(p);
}

// ── Adding Options ──────────────────────────────────────────────────────────

AP_NODISCARD APDEF ap_status ap_add_string(ap_parser* p, const char* long_opt,
                                           char short_opt, const char* help,
                                           const char* default_val) {
    if (!p || !long_opt) return AP_ERR_NULL_ARG;
    if (p->option_count >= AP_MAX_OPTIONS) return AP_ERR_PARSE;

    ap_option* o    = &p->options[p->option_count++];
    memset(o, 0, sizeof(*o));
    o->long_opt     = long_opt;
    o->short_opt    = short_opt;
    o->help         = help;
    o->type         = AP_OPT_STRING;
    o->default_str  = default_val;
    o->value_str    = default_val;
    return AP_OK;
}

AP_NODISCARD APDEF ap_status ap_add_int(ap_parser* p, const char* long_opt,
                                        char short_opt, const char* help,
                                        int64_t default_val) {
    if (!p || !long_opt) return AP_ERR_NULL_ARG;
    if (p->option_count >= AP_MAX_OPTIONS) return AP_ERR_PARSE;

    ap_option* o    = &p->options[p->option_count++];
    memset(o, 0, sizeof(*o));
    o->long_opt     = long_opt;
    o->short_opt    = short_opt;
    o->help         = help;
    o->type         = AP_OPT_INT;
    o->default_int  = default_val;
    o->value_int    = default_val;
    return AP_OK;
}

AP_NODISCARD APDEF ap_status ap_add_float(ap_parser* p, const char* long_opt,
                                          char short_opt, const char* help,
                                          double default_val) {
    if (!p || !long_opt) return AP_ERR_NULL_ARG;
    if (p->option_count >= AP_MAX_OPTIONS) return AP_ERR_PARSE;

    ap_option* o      = &p->options[p->option_count++];
    memset(o, 0, sizeof(*o));
    o->long_opt       = long_opt;
    o->short_opt      = short_opt;
    o->help           = help;
    o->type           = AP_OPT_FLOAT;
    o->default_float  = default_val;
    o->value_float    = default_val;
    return AP_OK;
}

AP_NODISCARD APDEF ap_status ap_add_bool(ap_parser* p, const char* long_opt,
                                         char short_opt, const char* help) {
    if (!p || !long_opt) return AP_ERR_NULL_ARG;
    if (p->option_count >= AP_MAX_OPTIONS) return AP_ERR_PARSE;

    ap_option* o  = &p->options[p->option_count++];
    memset(o, 0, sizeof(*o));
    o->long_opt   = long_opt;
    o->short_opt  = short_opt;
    o->help       = help;
    o->type       = AP_OPT_BOOL;
    o->value_bool = false;
    return AP_OK;
}

AP_NODISCARD APDEF ap_status ap_add_positional(ap_parser* p, const char* name,
                                               const char* help) {
    if (!p || !name) return AP_ERR_NULL_ARG;
    if (p->positional_def_count >= AP_MAX_POSITIONALS) return AP_ERR_PARSE;

    ap_positional_def* d = &p->positional_defs[p->positional_def_count++];
    d->name = name;
    d->help = help;
    return AP_OK;
}

// ── Required ────────────────────────────────────────────────────────────────

AP_NODISCARD APDEF ap_status ap_required(ap_parser* p, const char* long_opt) {
    if (!p || !long_opt) return AP_ERR_NULL_ARG;
    ap_option* o = ap__find_long(p, long_opt);
    if (!o) return AP_ERR_UNKNOWN_OPT;
    o->required = true;
    return AP_OK;
}

// ── Parsing ─────────────────────────────────────────────────────────────────

AP_NODISCARD APDEF ap_status ap_parse(ap_parser* p, int argc, char** argv) {
    if (!p) return AP_ERR_NULL_ARG;

    bool stop_opts = false;  // set by --

    for (int i = 1; i < argc; i++) {
        char* arg = argv[i];

        // -- separator: everything after is positional
        if (!stop_opts && strcmp(arg, "--") == 0) {
            stop_opts = true;
            continue;
        }

        // Long option: --key or --key=value
        if (!stop_opts && arg[0] == '-' && arg[1] == '-' && arg[2] != '\0') {
            char* name = arg + 2;
            char* eq   = strchr(name, '=');
            char  name_buf[256];

            if (eq) {
                size_t len = (size_t)(eq - name);
                if (len >= sizeof(name_buf)) len = sizeof(name_buf) - 1;
                memcpy(name_buf, name, len);
                name_buf[len] = '\0';
                name = name_buf;
            }

            ap_option* opt = ap__find_long(p, name);
            if (!opt) return AP_ERR_UNKNOWN_OPT;

            if (opt->type == AP_OPT_BOOL) {
                opt->is_set     = true;
                opt->value_bool = true;
            } else {
                const char* val = NULL;
                if (eq) {
                    val = eq + 1;
                } else if (i + 1 < argc) {
                    val = argv[++i];
                } else {
                    return AP_ERR_MISSING_VAL;
                }
                ap_status s = ap__set_value(opt, val);
                if (s != AP_OK) return s;
            }
            continue;
        }

        // Short option: -x or -xVALUE or stacked bools -abc
        if (!stop_opts && arg[0] == '-' && arg[1] != '\0' && arg[1] != '-') {
            for (int j = 1; arg[j] != '\0'; j++) {
                char c = arg[j];
                ap_option* opt = ap__find_short(p, c);
                if (!opt) return AP_ERR_UNKNOWN_OPT;

                if (opt->type == AP_OPT_BOOL) {
                    opt->is_set     = true;
                    opt->value_bool = true;
                    // continue to next char (stacked bools)
                } else {
                    // Non-bool short option: rest of arg is value, or next arg
                    const char* val = NULL;
                    if (arg[j + 1] != '\0') {
                        val = arg + j + 1;
                    } else if (i + 1 < argc) {
                        val = argv[++i];
                    } else {
                        return AP_ERR_MISSING_VAL;
                    }
                    ap_status s = ap__set_value(opt, val);
                    if (s != AP_OK) return s;
                    break;  // consumed rest of this arg
                }
            }
            continue;
        }

        // Positional argument
        if (p->positional_count >= AP_MAX_POSITIONALS)
            return AP_ERR_PARSE;
        p->positionals[p->positional_count++] = arg;
    }

    // Check required options
    for (int i = 0; i < p->option_count; i++) {
        if (p->options[i].required && !p->options[i].is_set)
            return AP_ERR_REQUIRED;
    }

    return AP_OK;
}

// ── Accessing Results ───────────────────────────────────────────────────────

APDEF const char* ap_get_string(const ap_parser* p, const char* long_opt) {
    if (!p || !long_opt) return NULL;
    const ap_option* o = ap__find_long(p, long_opt);
    return o ? o->value_str : NULL;
}

APDEF int64_t ap_get_int(const ap_parser* p, const char* long_opt) {
    if (!p || !long_opt) return 0;
    const ap_option* o = ap__find_long(p, long_opt);
    return o ? o->value_int : 0;
}

APDEF double ap_get_float(const ap_parser* p, const char* long_opt) {
    if (!p || !long_opt) return 0.0;
    const ap_option* o = ap__find_long(p, long_opt);
    return o ? o->value_float : 0.0;
}

APDEF bool ap_get_bool(const ap_parser* p, const char* long_opt) {
    if (!p || !long_opt) return false;
    const ap_option* o = ap__find_long(p, long_opt);
    return o ? o->value_bool : false;
}

APDEF bool ap_is_set(const ap_parser* p, const char* long_opt) {
    if (!p || !long_opt) return false;
    const ap_option* o = ap__find_long(p, long_opt);
    return o ? o->is_set : false;
}

APDEF const char* ap_get_positional(const ap_parser* p, int index) {
    if (!p || index < 0 || index >= p->positional_count) return NULL;
    return p->positionals[index];
}

APDEF int ap_positional_count(const ap_parser* p) {
    return p ? p->positional_count : 0;
}

// ── Help ────────────────────────────────────────────────────────────────────

APDEF void ap_print_usage(const ap_parser* p, FILE* f) {
    if (!p || !f) return;
    fprintf(f, "Usage: %s [options]", p->program_name ? p->program_name : "program");
    for (int i = 0; i < p->positional_def_count; i++) {
        fprintf(f, " <%s>", p->positional_defs[i].name);
    }
    fprintf(f, "\n");
}

APDEF void ap_print_help(const ap_parser* p, FILE* f) {
    if (!p || !f) return;

    ap_print_usage(p, f);

    if (p->description) {
        fprintf(f, "\n%s\n", p->description);
    }

    if (p->option_count > 0) {
        fprintf(f, "\nOptions:\n");
        for (int i = 0; i < p->option_count; i++) {
            const ap_option* o = &p->options[i];
            if (o->short_opt) {
                fprintf(f, "  -%c, --%-20s %s", o->short_opt, o->long_opt,
                        o->help ? o->help : "");
            } else {
                fprintf(f, "      --%-20s %s", o->long_opt,
                        o->help ? o->help : "");
            }
            if (o->required) fprintf(f, " (required)");
            fprintf(f, "\n");
        }
    }

    if (p->positional_def_count > 0) {
        fprintf(f, "\nPositional arguments:\n");
        for (int i = 0; i < p->positional_def_count; i++) {
            const ap_positional_def* d = &p->positional_defs[i];
            fprintf(f, "  %-24s %s\n", d->name, d->help ? d->help : "");
        }
    }
}

#endif // AP_IMPLEMENTATION
#endif // ARGPARSE_H
