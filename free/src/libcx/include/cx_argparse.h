/*
 * cx_argparse.h - Command-line argument parser.
 * Part of libcx. Pure C89.
 */

#ifndef CX_ARGPARSE_H
#define CX_ARGPARSE_H

#define CX_ARG_FLAG   0
#define CX_ARG_STRING 1
#define CX_ARG_INT    2

#define CX_ARG_OPTIONAL 0
#define CX_ARG_REQUIRED 1

#define CX_ARGPARSE_MAX_OPTS 64

typedef struct {
    char shortname;     /* single char, e.g. 'v' for -v */
    const char *longname;  /* e.g. "verbose" for --verbose */
    const char *help;
    int type;           /* CX_ARG_FLAG, CX_ARG_STRING, CX_ARG_INT */
    int required;
    int present;        /* set to 1 after parsing if found */
    union {
        int flag;
        const char *str;
        long ival;
    } val;
} cx_argopt;

typedef struct {
    cx_argopt opts[CX_ARGPARSE_MAX_OPTS];
    int nopts;
    const char *progname;
    const char *description;
    /* positional args after -- or after all options */
    int argc_rest;
    char **argv_rest;
} cx_argparse;

void cx_argparse_init(cx_argparse *p, const char *progname, const char *desc);
void cx_argparse_add(cx_argparse *p, char shortname, const char *longname,
                     const char *help, int type, int required);
int  cx_argparse_parse(cx_argparse *p, int argc, char **argv);
void cx_argparse_usage(cx_argparse *p);
cx_argopt *cx_argparse_get(cx_argparse *p, const char *longname);

#endif
