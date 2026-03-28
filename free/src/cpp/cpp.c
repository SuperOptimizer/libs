/*
 * cpp.c - Standalone C preprocessor for the free toolchain
 * Usage: free-cpp [-D name[=val]] [-U name] [-I dir] [-include file]
 *                 [-o output] input.c
 * Wraps the existing pp.c code in a standalone driver.
 * Pure C89. No external dependencies beyond the cc/ source files.
 */

#include "free.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- preprocessor interface (from pp.c) ---- */
extern void pp_init(const char *src, const char *filename, struct arena *a);
extern struct tok *pp_next(void);
extern void pp_add_include_path(const char *path);
extern void pp_add_cmdline_define(const char *def);
extern void pp_add_cmdline_undef(const char *name);
extern void pp_add_force_include(const char *path);
extern int pp_preprocess_to_file(const char *input_path,
                                  const char *output_path);

/* ---- globals required by cc modules ---- */
struct cc_std cc_std;
int cc_target_arch;
int cc_opt_level;
int cc_freestanding;
int cc_function_sections;
int cc_data_sections;
int cc_general_regs_only;
int cc_nostdinc;
int cc_pic_enabled;
int cc_debug_info;

/* ---- diagnostics (from diag.c) ---- */
extern void diag_init(void);

/* ---- standard level management ---- */
void cc_std_init(int level)
{
    memset(&cc_std, 0, sizeof(cc_std));
    cc_std.std_level = level;
    cc_std.feat = FEAT_LINE_COMMENTS;
    if ((level >= STD_C99 && level <= STD_C23) ||
        level == STD_GNU99 || level == STD_GNU11 || level == STD_GNU23) {
        cc_std.feat |= FEAT_LONG_LONG | FEAT_HEX_FLOAT | FEAT_BOOL
                     | FEAT_RESTRICT | FEAT_INLINE | FEAT_UCN
                     | FEAT_MIXED_DECL | FEAT_FOR_DECL | FEAT_VLA
                     | FEAT_DESIG_INIT | FEAT_COMPOUND_LIT
                     | FEAT_FLEX_ARRAY | FEAT_STATIC_ARRAY
                     | FEAT_VARIADIC_MACRO | FEAT_PRAGMA_OP
                     | FEAT_FUNC_MACRO | FEAT_EMPTY_MACRO_ARG;
        cc_std.feat2 |= FEAT2_NO_IMPLICIT_INT;
    }
    if ((level >= STD_C11 && level <= STD_C23) ||
        level == STD_GNU11 || level == STD_GNU23) {
        cc_std.feat |= FEAT_ALIGNAS | FEAT_ALIGNOF | FEAT_STATIC_ASSERT
                     | FEAT_NORETURN | FEAT_GENERIC | FEAT_ATOMIC
                     | FEAT_THREAD_LOCAL | FEAT_UNICODE_STR;
        cc_std.feat2 |= FEAT2_ANON_STRUCT;
    }
    if (level == STD_C23 || level == STD_GNU23) {
        cc_std.feat |= FEAT_BOOL_KW | FEAT_NULLPTR | FEAT_TYPEOF
                     | FEAT_BIN_LITERAL | FEAT_DIGIT_SEP
                     | FEAT_ATTR_SYNTAX;
        cc_std.feat2 |= FEAT2_CONSTEXPR | FEAT2_STATIC_ASSERT_NS
                      | FEAT2_EMPTY_INIT | FEAT2_LABEL_DECL
                      | FEAT2_UNNAMED_PARAM;
    }
    if (level == STD_GNU89) {
        cc_std.feat |= FEAT_INLINE | FEAT_LONG_LONG;
    }
}

int cc_has_feat(unsigned long f)
{
    return (cc_std.feat & f) != 0;
}

int cc_has_feat2(unsigned long f)
{
    return (cc_std.feat2 & f) != 0;
}

int cc_std_at_least(int level)
{
    int base;

    base = cc_std.std_level;
    if (base >= STD_GNU89) {
        base = base - STD_GNU89;
    }
    return base >= level;
}

/* ---- constants ---- */
#define MAX_INCLUDE_PATHS 64
#define MAX_DEFINES       64
#define MAX_UNDEFS        64
#define MAX_FORCE_INCLUDES 16

/* ---- usage ---- */
static void usage(void)
{
    fprintf(stderr,
        "Usage: free-cpp [options] <input.c>\n"
        "Options:\n"
        "  -D <name>[=val]  Define macro\n"
        "  -U <name>        Undefine macro\n"
        "  -I <dir>         Add include search path\n"
        "  -include <file>  Force-include a header\n"
        "  -o <file>        Output file (default: stdout)\n"
        "  -std=<standard>  Set language standard\n"
        "  -h               Show this help\n"
    );
}

/* ---- parse -std= flag ---- */
static int parse_std_flag(const char *arg)
{
    if (strcmp(arg, "c89") == 0 || strcmp(arg, "c90") == 0) {
        return STD_C89;
    }
    if (strcmp(arg, "c99") == 0) {
        return STD_C99;
    }
    if (strcmp(arg, "c11") == 0) {
        return STD_C11;
    }
    if (strcmp(arg, "c23") == 0) {
        return STD_C23;
    }
    if (strcmp(arg, "gnu89") == 0 || strcmp(arg, "gnu90") == 0) {
        return STD_GNU89;
    }
    if (strcmp(arg, "gnu99") == 0) {
        return STD_GNU99;
    }
    if (strcmp(arg, "gnu11") == 0) {
        return STD_GNU11;
    }
    if (strcmp(arg, "gnu23") == 0) {
        return STD_GNU23;
    }
    return -1;
}

/* ---- main ---- */
int main(int argc, char **argv)
{
    const char *input;
    const char *output;
    int std_level;
    int i;
    int ret;

    input = NULL;
    output = NULL;
    std_level = STD_C89;

    /* Handle --version before full arg parsing */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("free-cpp (free) 14.1.0\n");
            return 0;
        }
    }

    /* parse arguments */
    i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-D") == 0 && i + 1 < argc) {
            i++;
            pp_add_cmdline_define(argv[i]);
        } else if (strncmp(argv[i], "-D", 2) == 0 && argv[i][2] != '\0') {
            pp_add_cmdline_define(argv[i] + 2);
        } else if (strcmp(argv[i], "-U") == 0 && i + 1 < argc) {
            i++;
            pp_add_cmdline_undef(argv[i]);
        } else if (strncmp(argv[i], "-U", 2) == 0 && argv[i][2] != '\0') {
            pp_add_cmdline_undef(argv[i] + 2);
        } else if (strcmp(argv[i], "-I") == 0 && i + 1 < argc) {
            i++;
            pp_add_include_path(argv[i]);
        } else if (strncmp(argv[i], "-I", 2) == 0 && argv[i][2] != '\0') {
            pp_add_include_path(argv[i] + 2);
        } else if (strcmp(argv[i], "-include") == 0 && i + 1 < argc) {
            i++;
            pp_add_force_include(argv[i]);
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            i++;
            output = argv[i];
        } else if (strncmp(argv[i], "-std=", 5) == 0) {
            std_level = parse_std_flag(argv[i] + 5);
            if (std_level < 0) {
                fprintf(stderr, "free-cpp: unknown standard: %s\n",
                        argv[i] + 5);
                return 1;
            }
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "free-cpp: unknown option: %s\n", argv[i]);
            return 1;
        } else {
            input = argv[i];
        }
        i++;
    }

    if (input == NULL) {
        usage();
        return 1;
    }

    cc_std_init(std_level);
    diag_init();

    ret = pp_preprocess_to_file(input, output);
    return ret;
}
