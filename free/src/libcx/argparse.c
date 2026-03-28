/*
 * argparse.c - Command-line argument parser implementation.
 * Part of libcx. Pure C89.
 */

#include "cx_argparse.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void cx_argparse_init(cx_argparse *p, const char *progname, const char *desc)
{
    memset(p, 0, sizeof(*p));
    p->progname = progname;
    p->description = desc;
}

void cx_argparse_add(cx_argparse *p, char shortname, const char *longname,
                     const char *help, int type, int required)
{
    cx_argopt *opt;
    if (p->nopts >= CX_ARGPARSE_MAX_OPTS) return;

    opt = &p->opts[p->nopts++];
    memset(opt, 0, sizeof(*opt));
    opt->shortname = shortname;
    opt->longname = longname;
    opt->help = help;
    opt->type = type;
    opt->required = required;
    opt->present = 0;
}

static cx_argopt *find_short(cx_argparse *p, char c)
{
    int i;
    for (i = 0; i < p->nopts; i++) {
        if (p->opts[i].shortname == c) {
            return &p->opts[i];
        }
    }
    return NULL;
}

static cx_argopt *find_long(cx_argparse *p, const char *name)
{
    int i;
    for (i = 0; i < p->nopts; i++) {
        if (p->opts[i].longname && strcmp(p->opts[i].longname, name) == 0) {
            return &p->opts[i];
        }
    }
    return NULL;
}

static int parse_opt_value(cx_argopt *opt, const char *val)
{
    switch (opt->type) {
    case CX_ARG_FLAG:
        opt->val.flag = 1;
        break;
    case CX_ARG_STRING:
        if (!val) return -1;
        opt->val.str = val;
        break;
    case CX_ARG_INT:
        if (!val) return -1;
        opt->val.ival = strtol(val, NULL, 0);
        break;
    }
    opt->present = 1;
    return 0;
}

int cx_argparse_parse(cx_argparse *p, int argc, char **argv)
{
    int i;
    int j;

    p->argc_rest = 0;
    p->argv_rest = NULL;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) {
            /* Everything after -- is positional */
            p->argv_rest = &argv[i + 1];
            p->argc_rest = argc - i - 1;
            break;
        }

        if (argv[i][0] == '-' && argv[i][1] == '-') {
            /* Long option */
            const char *name = argv[i] + 2;
            const char *eq;
            cx_argopt *opt;
            char namebuf[128];

            eq = strchr(name, '=');
            if (eq) {
                size_t nlen = (size_t)(eq - name);
                if (nlen >= sizeof(namebuf)) nlen = sizeof(namebuf) - 1;
                memcpy(namebuf, name, nlen);
                namebuf[nlen] = '\0';
                opt = find_long(p, namebuf);
                if (!opt) {
                    fprintf(stderr, "Unknown option: --%s\n", namebuf);
                    return -1;
                }
                if (parse_opt_value(opt, eq + 1) != 0) {
                    fprintf(stderr, "Missing value for --%s\n", namebuf);
                    return -1;
                }
            } else {
                opt = find_long(p, name);
                if (!opt) {
                    fprintf(stderr, "Unknown option: --%s\n", name);
                    return -1;
                }
                if (opt->type != CX_ARG_FLAG) {
                    if (i + 1 >= argc) {
                        fprintf(stderr, "Missing value for --%s\n", name);
                        return -1;
                    }
                    if (parse_opt_value(opt, argv[++i]) != 0) return -1;
                } else {
                    parse_opt_value(opt, NULL);
                }
            }
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            /* Short options */
            const char *s = argv[i] + 1;
            while (*s) {
                cx_argopt *opt = find_short(p, *s);
                if (!opt) {
                    fprintf(stderr, "Unknown option: -%c\n", *s);
                    return -1;
                }
                if (opt->type != CX_ARG_FLAG) {
                    /* Value is rest of string or next arg */
                    if (*(s + 1)) {
                        if (parse_opt_value(opt, s + 1) != 0) return -1;
                        break;
                    } else {
                        if (i + 1 >= argc) {
                            fprintf(stderr, "Missing value for -%c\n", *s);
                            return -1;
                        }
                        if (parse_opt_value(opt, argv[++i]) != 0) return -1;
                        break;
                    }
                } else {
                    parse_opt_value(opt, NULL);
                }
                s++;
            }
        } else {
            /* Positional: collect rest */
            p->argv_rest = &argv[i];
            p->argc_rest = argc - i;
            break;
        }
    }

    /* Check required options */
    for (j = 0; j < p->nopts; j++) {
        if (p->opts[j].required && !p->opts[j].present) {
            fprintf(stderr, "Required option missing: ");
            if (p->opts[j].longname) {
                fprintf(stderr, "--%s\n", p->opts[j].longname);
            } else {
                fprintf(stderr, "-%c\n", p->opts[j].shortname);
            }
            return -1;
        }
    }

    return 0;
}

void cx_argparse_usage(cx_argparse *p)
{
    int i;
    fprintf(stderr, "Usage: %s [options]\n", p->progname);
    if (p->description) {
        fprintf(stderr, "%s\n", p->description);
    }
    fprintf(stderr, "\nOptions:\n");

    for (i = 0; i < p->nopts; i++) {
        cx_argopt *opt = &p->opts[i];
        fprintf(stderr, "  ");
        if (opt->shortname) {
            fprintf(stderr, "-%c", opt->shortname);
            if (opt->longname) fprintf(stderr, ", ");
        } else {
            fprintf(stderr, "    ");
        }
        if (opt->longname) {
            fprintf(stderr, "--%s", opt->longname);
        }
        if (opt->type == CX_ARG_STRING) {
            fprintf(stderr, " <string>");
        } else if (opt->type == CX_ARG_INT) {
            fprintf(stderr, " <int>");
        }
        if (opt->help) {
            fprintf(stderr, "\t%s", opt->help);
        }
        if (opt->required) {
            fprintf(stderr, " (required)");
        }
        fprintf(stderr, "\n");
    }
}

cx_argopt *cx_argparse_get(cx_argparse *p, const char *longname)
{
    int i;
    for (i = 0; i < p->nopts; i++) {
        if (p->opts[i].longname && strcmp(p->opts[i].longname, longname) == 0) {
            return &p->opts[i];
        }
    }
    return NULL;
}
