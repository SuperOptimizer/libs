/*
 * cc.c - Compiler driver for the free C compiler.
 * Orchestrates: preprocess -> parse -> codegen -> optimize -> assemble -> link.
 * Usage: free-cc [-o output] [-S] [-c] [-I dir] [-D name[=val]]
 *                [-std=c89|c99|c11|c23|gnu89|gnu99|gnu11|gnu23] input.c
 * Pure C89. No external dependencies.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#include <sys/mman.h>
#endif
#include "free.h"
#include "ir.h"

/* ---- target architecture ---- */
#define TARGET_AARCH64  0
#define TARGET_X86_64   1

/* global: current target architecture (default: aarch64) */
int cc_target_arch;

/* global: optimization level (0-3, set by -O flags) */
int cc_opt_level;

/* global: compiler flags for Task #25 */
int cc_freestanding;       /* -ffreestanding */
int cc_function_sections;  /* -ffunction-sections */
int cc_data_sections;      /* -fdata-sections */
int cc_general_regs_only;  /* -mgeneral-regs-only */
int cc_nostdinc;           /* -nostdinc */

/* global: flags that affect code generation correctness */
int cc_no_builtin;         /* -fno-builtin: disable builtin recognition */
int cc_omit_frame_pointer; /* -fomit-frame-pointer: omit FP (default 0) */

/* saved IR module for LTO serialization */
static struct ir_module *cc_ir_mod;

/* ---- forward declarations for compiler pipeline stages ---- */
extern void gen(struct node *prog, FILE *outfile);
extern struct node *parse(const char *src, const char *filename,
                          struct arena *a);
extern void pp_add_include_path(const char *path);
extern void pp_init(const char *src, const char *filename, struct arena *a);
extern struct tok *pp_next(void);
extern int pp_preprocess_to_file(const char *input_path,
                                  const char *output_path);
extern int opt_peephole(char *asm_text, int len);

/* ir_codegen.c: IR-based code generation (used at -O2+) */
extern void ir_codegen(struct ir_module *mod, FILE *out);

/* pp.c: command-line -D / -U / -include (stored, applied during pp_init) */
extern void pp_add_cmdline_define(const char *def);
extern void pp_add_cmdline_undef(const char *name);
extern void pp_add_force_include(const char *path);

/* pp.c: dependency tracking for -MD/-MMD */
extern void pp_dep_set_exclude_system(int exclude);
extern int pp_dep_get_count(void);
extern const char *pp_dep_get_file(int idx);
extern void pp_dep_write(FILE *out, const char *target);

/* pic.c */
extern int cc_pic_enabled;
extern void pic_emit_global_addr(FILE *f, const char *nm);
extern void pic_emit_string_addr(FILE *f, int id);

/* gen_x86.c */
extern void gen_x86(struct node *p, FILE *o);

/* gen.c */
extern void gen_set_source_info(const char *f, const char *d);

/* dwarf.c */
extern int cc_debug_info;

/* ---- global standard state ---- */
struct cc_std cc_std;

/* ---- standard level management ---- */

void cc_std_init(int level)
{
    memset(&cc_std, 0, sizeof(cc_std));
    cc_std.std_level = level;

    /* Always-on features matching GCC __-prefix extensions:
     * line comments, designated init, restrict, inline, long long,
     * _Bool, compound literals, flex arrays, __func__, variadic
     * macros, _Complex. Needed for kernel code and torture tests. */
    cc_std.feat = FEAT_LINE_COMMENTS | FEAT_DESIG_INIT | FEAT_RESTRICT
                | FEAT_INLINE | FEAT_LONG_LONG | FEAT_BOOL
                | FEAT_COMPOUND_LIT | FEAT_FLEX_ARRAY
                | FEAT_FUNC_MACRO | FEAT_VARIADIC_MACRO
                | FEAT_MIXED_DECL | FEAT_FOR_DECL | FEAT_VLA
                | FEAT_HEX_FLOAT | FEAT_BIN_LITERAL
                | FEAT_ALIGNAS | FEAT_ALIGNOF
                | FEAT_STATIC_ASSERT | FEAT_NORETURN
                | FEAT_GENERIC | FEAT_TYPEOF
                | FEAT_UNICODE_STR;
    cc_std.feat2 = FEAT2_COMPLEX | FEAT2_ANON_STRUCT
                 | FEAT2_EMPTY_INIT;

    /* C99 features */
    if ((level >= STD_C99 && level <= STD_C23) ||
        level == STD_GNU99 || level == STD_GNU11 ||
        level == STD_GNU23) {
        cc_std.feat |= FEAT_LONG_LONG | FEAT_HEX_FLOAT | FEAT_BOOL
                     | FEAT_RESTRICT | FEAT_INLINE | FEAT_UCN
                     | FEAT_MIXED_DECL | FEAT_FOR_DECL | FEAT_VLA
                     | FEAT_DESIG_INIT | FEAT_COMPOUND_LIT
                     | FEAT_FLEX_ARRAY | FEAT_STATIC_ARRAY
                     | FEAT_VARIADIC_MACRO | FEAT_PRAGMA_OP
                     | FEAT_FUNC_MACRO | FEAT_EMPTY_MACRO_ARG;
        cc_std.feat2 |= FEAT2_NO_IMPLICIT_INT;
    }

    /* C11 features */
    if ((level >= STD_C11 && level <= STD_C23) ||
        level == STD_GNU11 || level == STD_GNU23) {
        cc_std.feat |= FEAT_ALIGNAS | FEAT_ALIGNOF | FEAT_STATIC_ASSERT
                     | FEAT_NORETURN | FEAT_GENERIC | FEAT_ATOMIC
                     | FEAT_THREAD_LOCAL | FEAT_UNICODE_STR;
        cc_std.feat2 |= FEAT2_ANON_STRUCT;
    }

    /* C23 features */
    if (level == STD_C23 || level == STD_GNU23) {
        cc_std.feat |= FEAT_BOOL_KW | FEAT_NULLPTR | FEAT_TYPEOF
                     | FEAT_BIN_LITERAL | FEAT_DIGIT_SEP
                     | FEAT_ATTR_SYNTAX;
        cc_std.feat2 |= FEAT2_CONSTEXPR | FEAT2_STATIC_ASSERT_NS
                      | FEAT2_EMPTY_INIT | FEAT2_LABEL_DECL
                      | FEAT2_UNNAMED_PARAM;
    }

    /* GNU extensions add on top of base standard.
     * GCC makes typeof, _Generic, _Static_assert, _Alignof, _Alignas,
     * and _Noreturn available in all -std=gnuXX modes. */
    if (level >= STD_GNU89) {
        cc_std.feat |= FEAT_TYPEOF | FEAT_GENERIC | FEAT_STATIC_ASSERT
                     | FEAT_ALIGNOF | FEAT_ALIGNAS | FEAT_NORETURN;
    }
    if (level == STD_GNU89) {
        cc_std.feat |= FEAT_INLINE | FEAT_LONG_LONG | FEAT_RESTRICT
                     | FEAT_DESIG_INIT | FEAT_COMPOUND_LIT
                     | FEAT_VARIADIC_MACRO;
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
    /* normalize GNU levels to their C base */
    if (base >= STD_GNU89) {
        base = base - STD_GNU89;
    }
    return base >= level;
}

/*
 * parse_std_flag - parse a -std= argument, return the level.
 * Returns -1 if unrecognized.
 */
static int parse_std_flag(const char *arg)
{
    if (strcmp(arg, "c89") == 0 || strcmp(arg, "c90") == 0 ||
        strcmp(arg, "iso9899:1990") == 0) {
        return STD_C89;
    }
    if (strcmp(arg, "c99") == 0 || strcmp(arg, "c9x") == 0 ||
        strcmp(arg, "iso9899:1999") == 0) {
        return STD_C99;
    }
    if (strcmp(arg, "c11") == 0 || strcmp(arg, "c1x") == 0 ||
        strcmp(arg, "iso9899:2011") == 0) {
        return STD_C11;
    }
    if (strcmp(arg, "c23") == 0 || strcmp(arg, "c2x") == 0 ||
        strcmp(arg, "iso9899:2024") == 0) {
        return STD_C23;
    }
    if (strcmp(arg, "gnu89") == 0 || strcmp(arg, "gnu90") == 0) {
        return STD_GNU89;
    }
    if (strcmp(arg, "gnu99") == 0 || strcmp(arg, "gnu9x") == 0) {
        return STD_GNU99;
    }
    if (strcmp(arg, "gnu11") == 0 || strcmp(arg, "gnu1x") == 0) {
        return STD_GNU11;
    }
    if (strcmp(arg, "gnu23") == 0 || strcmp(arg, "gnu2x") == 0) {
        return STD_GNU23;
    }
    return -1;
}

/* ---- constants ---- */
#define MAX_INCLUDE_PATHS 64
#define MAX_DEFINES       64
#define MAX_UNDEFS        64
#define MAX_FORCE_INCLUDES 16
#define MAX_INPUTS        256
#define PATH_BUF          4096
#define DEFAULT_ARENA_SIZE (768UL * 1024 * 1024)

static unsigned long cc_arena_size(void)
{
    const char *env;
    unsigned long size;
    char *end;

    env = getenv("FREE_CC_ARENA_SIZE");
    if (env == NULL || env[0] == '\0') {
        return DEFAULT_ARENA_SIZE;
    }

    size = strtoul(env, &end, 0);
    if (end == env || size == 0) {
        return DEFAULT_ARENA_SIZE;
    }
    return size;
}

/* ---- large allocation helpers ---- */
static char *arena_mmap(unsigned long sz)
{
#ifndef _WIN32
    char *p;
    p = (char *)mmap(NULL, sz, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) {
        return NULL;
    }
    return p;
#else
    return (char *)malloc(sz);
#endif
}

static void arena_munmap(char *p, unsigned long sz)
{
#ifndef _WIN32
    munmap(p, sz);
#else
    free(p);
    (void)sz;
#endif
}

/*
 * has_inline_asm - recursively check if AST contains ND_GCC_ASM nodes.
 * Returns 1 if inline asm found, 0 otherwise.
 */
static int has_inline_asm(struct node *n)
{
    if (n == NULL) {
        return 0;
    }
    if (n->kind == ND_GCC_ASM) {
        return 1;
    }
    if (has_inline_asm(n->lhs) || has_inline_asm(n->rhs)) {
        return 1;
    }
    if (has_inline_asm(n->body) || has_inline_asm(n->then_)) {
        return 1;
    }
    if (has_inline_asm(n->els) || has_inline_asm(n->init)) {
        return 1;
    }
    if (has_inline_asm(n->cond) || has_inline_asm(n->inc)) {
        return 1;
    }
    if (has_inline_asm(n->next)) {
        return 1;
    }
    return 0;
}

/* ---- input language modes ---- */
#define LANG_AUTO  0   /* detect from file extension */
#define LANG_C     1   /* -x c */
#define LANG_ASM   2   /* -x assembler (.s, no preprocessing) */
#define LANG_ASM_CPP 3 /* -x assembler-with-cpp (.S, preprocess) */

/* ---- compiler options ---- */
struct cc_opts {
    const char *input;             /* first input (for compat) */
    const char *inputs[MAX_INPUTS];/* all input files */
    int num_inputs;
    const char *output;
    int stop_after_pp;     /* -E: preprocess only */
    int stop_after_asm;    /* -S: emit assembly only */
    int stop_after_obj;    /* -c: emit object file only */
    int lang_mode;         /* LANG_AUTO, LANG_C, LANG_ASM, LANG_ASM_CPP */
    const char *include_paths[MAX_INCLUDE_PATHS];
    int num_include_paths;
    const char *defines[MAX_DEFINES];
    int num_defines;
    const char *undefs[MAX_UNDEFS];
    int num_undefs;
    const char *force_includes[MAX_FORCE_INCLUDES];
    int num_force_includes;
    int std_level;         /* STD_C89 .. STD_GNU23 */
    int enable_lto;        /* -flto: embed IR in .o for LTO */
    int target;            /* TARGET_AARCH64 or TARGET_X86_64 */
    int debug_info;        /* -g: emit DWARF debug info */
    int freestanding;      /* -ffreestanding */
    int nostdinc;          /* -nostdinc */
    int fno_common;        /* -fno-common (default behavior) */
    int func_sections;     /* -ffunction-sections */
    int data_sections;     /* -fdata-sections */
    int general_regs_only; /* -mgeneral-regs-only */
    int no_builtin;        /* -fno-builtin */
    int omit_frame_pointer;/* -fomit-frame-pointer */
    int opt_size;          /* -Os: optimize for size */
    int opt_debug;         /* -Og: optimize for debugging */
    int gen_deps;          /* -MD: generate .d dependency file */
    int gen_deps_nosys;    /* -MMD: deps excluding system headers */
    const char *dep_file;  /* -MF: explicit dependency output file */
    const char *dep_target;/* -MT: explicit target in dependency file */
    int suppress_warnings; /* -w: suppress all warnings */
    int suppress_line_markers; /* -P: no line markers in -E output */
};

/* ---- helper functions ---- */

static void usage(void)
{
    fprintf(stderr,
        "Usage: free-cc [options] <input.c>\n"
        "Options:\n"
        "  -o <file>        Output file name\n"
        "  -E               Preprocess only\n"
        "  -S               Output assembly\n"
        "  -c               Output object file\n"
        "  -I <dir>         Add include path\n"
        "  -D <name>[=val]  Predefine macro\n"
        "  -U <name>        Undefine macro\n"
    );
    fprintf(stderr,
        "  -include <file>  Force-include file\n"
        "  -nostdinc        No standard include paths\n"
        "  -ffreestanding   Freestanding environment\n"
        "  -std=<standard>  Set standard "
        "(c89,c99,c11,c23,gnu*)\n"
        "  -target <arch>   Target (aarch64, x86_64)\n"
        "  -flto            Link-time optimization\n"
        "  -fPIC            Position-independent code\n"
    );
    fprintf(stderr,
        "  -ffunction-sections  One section per function\n"
        "  -fdata-sections  One section per data symbol\n"
        "  -mgeneral-regs-only  No FP/SIMD registers\n"
        "  -g               Emit debug info\n"
        "  -h               Show this help\n"
    );
}

/*
 * replace_ext - replace the file extension of path.
 * Returns a pointer to a static buffer.
 */
static const char *replace_ext(const char *path, const char *new_ext)
{
    static char buf[PATH_BUF];
    const char *dot;
    size_t baselen;
    size_t extlen;

    dot = strrchr(path, '.');
    if (dot != NULL) {
        baselen = (size_t)(dot - path);
    } else {
        baselen = strlen(path);
    }

    extlen = strlen(new_ext);
    if (baselen + extlen + 1 >= PATH_BUF) {
        fprintf(stderr, "free-cc: path too long\n");
        exit(1);
    }

    memcpy(buf, path, baselen);
    memcpy(buf + baselen, new_ext, extlen);
    buf[baselen + extlen] = '\0';
    return buf;
}

/*
 * read_file - read entire file into a malloc'd buffer.
 * Returns NULL on failure. Sets *out_len if non-NULL.
 * Caller must free the returned buffer.
 */
static char *read_file(const char *path, long *out_len)
{
    FILE *f;
    long len;
    char *buf;
    size_t nread;

    f = fopen(path, "r");
    if (f == NULL) {
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    len = ftell(f);
    if (len < 0) {
        fclose(f);
        return NULL;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    buf = (char *)malloc((size_t)len + 2);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }

    nread = fread(buf, 1, (size_t)len, f);
    fclose(f);

    /* ensure newline termination and null terminator */
    if (nread > 0 && buf[nread - 1] != '\n') {
        buf[nread] = '\n';
        nread++;
    }
    buf[nread] = '\0';

    if (out_len != NULL) {
        *out_len = (long)nread;
    }
    return buf;
}

/*
 * run_command - execute a shell command. Returns exit status.
 */
static int run_command(const char *cmd)
{
    int status;

    status = system(cmd);
    if (status < 0) {
        fprintf(stderr, "free-cc: failed to run: %s\n", cmd);
        return 1;
    }
    /* extract exit code from wait status */
    return (status >> 8) & 0xff;
}

static void write_depfile(const char *df, const char *dt)
{
    FILE *depout;

    depout = fopen(df, "w");
    if (depout != NULL) {
        pp_dep_write(depout, dt);
        fclose(depout);
    }
}

/*
 * make_tmppath - build a temporary file path with given suffix.
 * Uses a static counter for uniqueness.
 * Returns pointer to a static buffer.
 */
static const char *make_tmppath(const char *suffix)
{
    static char buf[PATH_BUF];
    static int counter = 0;

    sprintf(buf, "/tmp/free-cc-%d-%d%s", (int)getpid(), counter, suffix);
    counter++;
    return buf;
}

/*
 * read_stdin_to_tmpfile - read all of stdin into a temp file.
 * Returns the path to the temp file (static buffer), or NULL on failure.
 */
static const char *read_stdin_to_tmpfile(void)
{
    static char stdin_tmp[PATH_BUF];
    FILE *f;
    char buf[4096];
    size_t n;

    sprintf(stdin_tmp, "/tmp/free-cc-stdin-%d.c", (int)getpid());
    f = fopen(stdin_tmp, "w");
    if (f == NULL) {
        fprintf(stderr, "free-cc: cannot create temp file for stdin\n");
        return NULL;
    }
    while ((n = fread(buf, 1, sizeof(buf), stdin)) > 0) {
        fwrite(buf, 1, n, f);
    }
    fclose(f);
    return stdin_tmp;
}

/* ---- response file expansion ---- */

/*
 * expand_response_file - read @file and split into argv-style tokens.
 * Splits on whitespace, supports double-quoted strings.
 * Returns number of arguments written to out_argv (up to max_args).
 */
static int expand_response_file(const char *path,
                                char **out_argv, int max_args)
{
    char *buf;
    long len;
    int argc_out;
    char *p;
    char *end;
    char *tok_start;

    buf = read_file(path, &len);
    if (buf == NULL) {
        fprintf(stderr, "free-cc: cannot read response file '%s'\n",
                path);
        return 0;
    }

    argc_out = 0;
    p = buf;
    end = buf + len;
    while (p < end && argc_out < max_args) {
        /* skip whitespace */
        while (p < end && (*p == ' ' || *p == '\t' ||
               *p == '\n' || *p == '\r')) {
            p++;
        }
        if (p >= end) {
            break;
        }
        if (*p == '"') {
            /* quoted argument */
            p++;
            tok_start = p;
            while (p < end && *p != '"') {
                p++;
            }
            *p = '\0';
            out_argv[argc_out++] = tok_start;
            if (p < end) {
                p++;
            }
        } else {
            /* unquoted argument */
            tok_start = p;
            while (p < end && *p != ' ' && *p != '\t' &&
                   *p != '\n' && *p != '\r') {
                p++;
            }
            if (p < end) {
                *p = '\0';
                p++;
            }
            out_argv[argc_out++] = tok_start;
        }
    }
    return argc_out;
}

/* ---- command-line parsing ---- */

static int parse_args(int argc, char **argv, struct cc_opts *opts);

/*
 * expand_argv - expand @responsefile arguments in argv.
 * Replaces @file entries with their contents. Returns new argc.
 * out_argv must have room for max_args entries.
 */
#define RESP_MAX_ARGS 4096

static int expand_argv(int argc, char **argv,
                       char **out_argv, int max_args)
{
    int i;
    int out_argc;

    out_argc = 0;
    for (i = 0; i < argc && out_argc < max_args; i++) {
        if (argv[i][0] == '@' && argv[i][1] != '\0') {
            int n;
            n = expand_response_file(argv[i] + 1,
                                     out_argv + out_argc,
                                     max_args - out_argc);
            out_argc += n;
        } else {
            out_argv[out_argc++] = argv[i];
        }
    }
    return out_argc;
}

static int parse_args(int argc, char **argv, struct cc_opts *opts)
{
    int i;
    char *expanded[RESP_MAX_ARGS];
    int exp_argc;

    /* expand @responsefile arguments */
    exp_argc = expand_argv(argc, argv, expanded, RESP_MAX_ARGS);
    argv = expanded;
    argc = exp_argc;

    memset(opts, 0, sizeof(*opts));
    opts->std_level = STD_C89;  /* default to C89 */

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "free-cc: -o requires argument\n");
                return 1;
            }
            i++;
            opts->output = argv[i];

        } else if (strcmp(argv[i], "-E") == 0) {
            opts->stop_after_pp = 1;

        } else if (strcmp(argv[i], "-S") == 0) {
            opts->stop_after_asm = 1;

        } else if (strcmp(argv[i], "-c") == 0) {
            opts->stop_after_obj = 1;

        } else if (strcmp(argv[i], "-O0") == 0) {
            cc_opt_level = 0;
        } else if (strcmp(argv[i], "-O1") == 0) {
            cc_opt_level = 1;
        } else if (strcmp(argv[i], "-O2") == 0) {
            cc_opt_level = 2;
        } else if (strcmp(argv[i], "-O3") == 0) {
            cc_opt_level = 3;

        } else if (strcmp(argv[i], "-flto") == 0) {
            opts->enable_lto = 1;

        } else if (strcmp(argv[i], "-fPIC") == 0 ||
                   strcmp(argv[i], "-fpic") == 0) {
            cc_pic_enabled = 1;

        } else if (strcmp(argv[i], "-shared") == 0) {
            cc_pic_enabled = 1;

        } else if (strcmp(argv[i], "-g") == 0) {
            opts->debug_info = 1;

        } else if (strcmp(argv[i], "-target") == 0 ||
                   strncmp(argv[i], "--target=", 9) == 0) {
            const char *targ;
            if (strncmp(argv[i], "--target=", 9) == 0) {
                targ = argv[i] + 9;
            } else {
                if (i + 1 >= argc) {
                    fprintf(stderr,
                        "free-cc: -target requires argument\n");
                    return 1;
                }
                i++;
                targ = argv[i];
            }
            if (strcmp(targ, "aarch64") == 0 ||
                strcmp(targ, "arm64") == 0) {
                opts->target = TARGET_AARCH64;
            } else if (strcmp(targ, "x86_64") == 0 ||
                       strcmp(targ, "x86-64") == 0 ||
                       strcmp(targ, "x64") == 0) {
                opts->target = TARGET_X86_64;
            } else {
                fprintf(stderr,
                    "free-cc: unknown target '%s'\n", targ);
                return 1;
            }

        } else if (strcmp(argv[i], "-I") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "free-cc: -I requires argument\n");
                return 1;
            }
            i++;
            if (opts->num_include_paths >= MAX_INCLUDE_PATHS) {
                fprintf(stderr, "free-cc: too many -I paths\n");
                return 1;
            }
            opts->include_paths[opts->num_include_paths] = argv[i];
            opts->num_include_paths++;

        } else if (argv[i][0] == '-' && argv[i][1] == 'I' &&
                   argv[i][2] != '\0') {
            if (opts->num_include_paths >= MAX_INCLUDE_PATHS) {
                fprintf(stderr, "free-cc: too many -I paths\n");
                return 1;
            }
            opts->include_paths[opts->num_include_paths] =
                argv[i] + 2;
            opts->num_include_paths++;

        } else if (strcmp(argv[i], "-D") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "free-cc: -D requires argument\n");
                return 1;
            }
            i++;
            if (opts->num_defines >= MAX_DEFINES) {
                fprintf(stderr, "free-cc: too many -D defines\n");
                return 1;
            }
            opts->defines[opts->num_defines] = argv[i];
            opts->num_defines++;

        } else if (argv[i][0] == '-' && argv[i][1] == 'D' &&
                   argv[i][2] != '\0') {
            if (opts->num_defines >= MAX_DEFINES) {
                fprintf(stderr, "free-cc: too many -D defines\n");
                return 1;
            }
            opts->defines[opts->num_defines] = argv[i] + 2;
            opts->num_defines++;

        } else if (strncmp(argv[i], "-std=", 5) == 0) {
            int sl;
            sl = parse_std_flag(argv[i] + 5);
            if (sl < 0) {
                fprintf(stderr,
                    "free-cc: unrecognized standard '%s'\n",
                    argv[i] + 5);
                return 1;
            }
            opts->std_level = sl;

        } else if (strcmp(argv[i], "-U") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "free-cc: -U requires argument\n");
                return 1;
            }
            i++;
            if (opts->num_undefs >= MAX_UNDEFS) {
                fprintf(stderr, "free-cc: too many -U undefs\n");
                return 1;
            }
            opts->undefs[opts->num_undefs] = argv[i];
            opts->num_undefs++;

        } else if (argv[i][0] == '-' && argv[i][1] == 'U' &&
                   argv[i][2] != '\0') {
            if (opts->num_undefs >= MAX_UNDEFS) {
                fprintf(stderr, "free-cc: too many -U undefs\n");
                return 1;
            }
            opts->undefs[opts->num_undefs] = argv[i] + 2;
            opts->num_undefs++;

        } else if (strcmp(argv[i], "-include") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr,
                    "free-cc: -include requires argument\n");
                return 1;
            }
            i++;
            if (opts->num_force_includes >= MAX_FORCE_INCLUDES) {
                fprintf(stderr,
                    "free-cc: too many -include files\n");
                return 1;
            }
            opts->force_includes[opts->num_force_includes] =
                argv[i];
            opts->num_force_includes++;

        } else if (strcmp(argv[i], "-nostdinc") == 0) {
            opts->nostdinc = 1;

        } else if (strcmp(argv[i], "-ffreestanding") == 0) {
            opts->freestanding = 1;

        } else if (strcmp(argv[i], "-mgeneral-regs-only") == 0) {
            opts->general_regs_only = 1;

        } else if (strcmp(argv[i], "-fno-common") == 0) {
            opts->fno_common = 1;

        } else if (strcmp(argv[i], "-ffunction-sections") == 0) {
            opts->func_sections = 1;

        } else if (strcmp(argv[i], "-fdata-sections") == 0) {
            opts->data_sections = 1;

        } else if (strcmp(argv[i], "-fno-builtin") == 0) {
            opts->no_builtin = 1;

        } else if (strncmp(argv[i], "-fno-builtin-", 13) == 0) {
            /* -fno-builtin-memcpy etc: accept, set global flag */
            opts->no_builtin = 1;

        } else if (strcmp(argv[i], "-fomit-frame-pointer") == 0) {
            opts->omit_frame_pointer = 1;

        } else if (strcmp(argv[i], "-fno-omit-frame-pointer") == 0) {
            opts->omit_frame_pointer = 0;

        } else if (strcmp(argv[i], "-fno-PIC") == 0 ||
                   strcmp(argv[i], "-fno-pic") == 0) {
            cc_pic_enabled = 0;

        } else if (strcmp(argv[i], "-fno-strict-aliasing") == 0 ||
                   strcmp(argv[i],
                       "-fno-delete-null-pointer-checks") == 0 ||
                   strcmp(argv[i],
                       "-fno-stack-protector") == 0 ||
                   strcmp(argv[i],
                       "-fno-strict-overflow") == 0 ||
                   strcmp(argv[i],
                       "-fno-allow-store-data-races") == 0 ||
                   strcmp(argv[i],
                       "-fno-optimize-sibling-calls") == 0 ||
                   strcmp(argv[i],
                       "-fconserve-stack") == 0 ||
                   strcmp(argv[i],
                       "-fno-inline-functions-called-once") == 0 ||
                   strcmp(argv[i],
                       "-fno-partial-inlining") == 0 ||
                   strcmp(argv[i],
                       "-fno-ipa-sra") == 0 ||
                   strcmp(argv[i],
                       "-fno-ipa-cp-clone") == 0 ||
                   strcmp(argv[i],
                       "-fno-ipa-bit-cp") == 0 ||
                   strcmp(argv[i],
                       "-fno-gcse") == 0 ||
                   strcmp(argv[i],
                       "-fno-tree-vectorize") == 0 ||
                   strcmp(argv[i],
                       "-fno-reorder-blocks") == 0 ||
                   strcmp(argv[i],
                       "-fno-asynchronous-unwind-tables") == 0 ||
                   strcmp(argv[i],
                       "-fno-unwind-tables") == 0 ||
                   strcmp(argv[i],
                       "-fno-exceptions") == 0 ||
                   strcmp(argv[i],
                       "-fno-jump-tables") == 0 ||
                   strcmp(argv[i],
                       "-fverbose-asm") == 0 ||
                   strcmp(argv[i],
                       "-fms-extensions") == 0 ||
                   strcmp(argv[i],
                       "-fno-stack-clash-protection") == 0) {
            /* accepted as no-ops */

        } else if (strncmp(argv[i],
                       "-ftrivial-auto-var-init=", 23) == 0 ||
                   strncmp(argv[i],
                       "-fzero-init-padding-bits=", 24) == 0 ||
                   strncmp(argv[i],
                       "-fmin-function-alignment=", 24) == 0 ||
                   strncmp(argv[i],
                       "-fstrict-flex-arrays=", 20) == 0 ||
                   strncmp(argv[i],
                       "-fdiagnostics-show-context=", 26) == 0 ||
                   strncmp(argv[i],
                       "-fmacro-prefix-map=", 18) == 0 ||
                   strncmp(argv[i],
                       "-ffile-prefix-map=", 17) == 0 ||
                   strncmp(argv[i],
                       "-fvisibility=", 13) == 0) {
            /* -f flags with =value: accepted as no-ops */

        } else if (strcmp(argv[i], "-MD") == 0) {
            opts->gen_deps = 1;

        } else if (strcmp(argv[i], "-MMD") == 0) {
            opts->gen_deps = 1;
            opts->gen_deps_nosys = 1;

        } else if (strcmp(argv[i], "-MF") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "free-cc: -MF requires argument\n");
                return 1;
            }
            i++;
            opts->dep_file = argv[i];

        } else if (strcmp(argv[i], "-MT") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "free-cc: -MT requires argument\n");
                return 1;
            }
            i++;
            opts->dep_target = argv[i];

        } else if (strncmp(argv[i], "-Wp,", 4) == 0) {
            /*
             * -Wp,OPT passes OPT to preprocessor.
             * Kbuild uses -Wp,-MD,file and -Wp,-MMD,file.
             * Parse comma-separated options.
             */
            const char *wp = argv[i] + 4;
            while (*wp != '\0') {
                if (strncmp(wp, "-MD,", 4) == 0) {
                    opts->gen_deps = 1;
                    opts->dep_file = wp + 4;
                    break;
                } else if (strncmp(wp, "-MMD,", 5) == 0) {
                    opts->gen_deps = 1;
                    opts->gen_deps_nosys = 1;
                    opts->dep_file = wp + 5;
                    break;
                } else if (strcmp(wp, "-MD") == 0) {
                    opts->gen_deps = 1;
                    break;
                } else if (strcmp(wp, "-MMD") == 0) {
                    opts->gen_deps = 1;
                    opts->gen_deps_nosys = 1;
                    break;
                }
                /* skip to next comma-separated option */
                while (*wp != ',' && *wp != '\0') {
                    wp++;
                }
                if (*wp == ',') {
                    wp++;
                }
            }

        } else if (strcmp(argv[i], "-w") == 0) {
            opts->suppress_warnings = 1;

        } else if (strcmp(argv[i], "-nostdlib") == 0 ||
                   strcmp(argv[i], "-no-pie") == 0 ||
                   strcmp(argv[i], "-fno-PIE") == 0 ||
                   strcmp(argv[i], "-fno-pie") == 0) {
            /* accepted as no-ops for kernel build compat */

        } else if (strncmp(argv[i], "--param=", 8) == 0) {
            /* --param=name=value: accepted as no-op */

        } else if (strcmp(argv[i], "--param") == 0) {
            /* --param name=value: skip argument */
            if (i + 1 < argc) {
                i++;
            }

        } else if (strcmp(argv[i], "-mlittle-endian") == 0 ||
                   strcmp(argv[i], "-mno-outline-atomics") == 0 ||
                   strncmp(argv[i], "-mabi=", 6) == 0 ||
                   strncmp(argv[i],
                       "-mbranch-protection=", 20) == 0) {
            /* aarch64 target flags: accepted as no-ops */

        } else if (strncmp(argv[i], "-Wa,", 4) == 0) {
            /* assembler pass-through flags */
            if (strstr(argv[i], "--version") != NULL) {
                /* Kbuild probes the assembler version this way */
                printf("GNU assembler (free toolchain) 2.40\n");
            }
            /* other -Wa, flags: accepted as no-op */

        } else if (strncmp(argv[i], "-Wl,", 4) == 0) {
            /* linker pass-through flags: accepted as no-op */

        } else if (strcmp(argv[i], "-l") == 0) {
            /* -l library: skip the argument */
            if (i + 1 < argc) {
                i++;
            }

        } else if (argv[i][0] == '-' && argv[i][1] == 'l' &&
                   argv[i][2] != '\0') {
            /* -llibrary: accepted as no-op */

        } else if (strcmp(argv[i], "-L") == 0) {
            /* -L path: skip the argument */
            if (i + 1 < argc) {
                i++;
            }

        } else if (argv[i][0] == '-' && argv[i][1] == 'L' &&
                   argv[i][2] != '\0') {
            /* -Lpath: accepted as no-op */

        } else if (strcmp(argv[i], "-P") == 0) {
            opts->suppress_line_markers = 1;
            /* no-op: our preprocessor doesn't emit line markers */

        } else if (strcmp(argv[i], "-pipe") == 0) {
            /* accepted as no-op */

        } else if (strcmp(argv[i], "-x") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "free-cc: -x requires argument\n");
                return 1;
            }
            i++;
            if (strcmp(argv[i], "c") == 0) {
                opts->lang_mode = LANG_C;
            } else if (strcmp(argv[i], "assembler-with-cpp") == 0) {
                opts->lang_mode = LANG_ASM_CPP;
            } else if (strcmp(argv[i], "assembler") == 0) {
                opts->lang_mode = LANG_ASM;
            }
            /* other values silently accepted */

        } else if (strcmp(argv[i], "-Os") == 0) {
            cc_opt_level = 2; /* treat -Os like -O2 */
            opts->opt_size = 1;

        } else if (strcmp(argv[i], "-Og") == 0) {
            cc_opt_level = 0; /* treat -Og like -O0 */
            opts->opt_debug = 1;

        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            usage();
            exit(0);

        } else if (argv[i][0] == '-' && argv[i][1] == 'W') {
            /* parse warning flags; accept unknown ones silently */
            diag_parse_warning_flag(argv[i]);

        } else if (argv[i][0] == '-' && argv[i][1] == 'f') {
            /* try -ferror-limit=N, else ignore unknown -f flags */
            diag_parse_error_limit(argv[i]);

        } else if (argv[i][0] == '-' && argv[i][1] == 'm') {
            /* silently ignore unknown -m* flags */

        } else if (argv[i][0] == '-' && argv[i][1] == 'O') {
            /* silently ignore unknown -O* flags */

        } else if (strcmp(argv[i], "-") == 0) {
            /* stdin input */
            if (opts->num_inputs >= MAX_INPUTS) {
                fprintf(stderr,
                    "free-cc: too many input files "
                    "(max %d)\n", MAX_INPUTS);
                return 1;
            }
            opts->inputs[opts->num_inputs++] = "-";
            if (opts->input == NULL) {
                opts->input = "-";
            }

        } else if (argv[i][0] == '-') {
            /* warn about unknown flags but don't error */
            if (!opts->suppress_warnings) {
                fprintf(stderr,
                    "free-cc: warning: unknown option '%s'\n",
                    argv[i]);
            }
            /*
             * If the next arg looks like a value for this flag
             * (doesn't start with - and isn't a source file), skip it.
             */
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                const char *nx = argv[i + 1];
                int nxlen = (int)strlen(nx);
                int is_src = 0;
                if (nxlen >= 2 &&
                    nx[nxlen - 2] == '.' &&
                    (nx[nxlen - 1] == 'c' || nx[nxlen - 1] == 's' ||
                     nx[nxlen - 1] == 'S')) {
                    is_src = 1;
                }
                if (!is_src) {
                    i++;
                }
            }

        } else {
            if (opts->num_inputs >= MAX_INPUTS) {
                fprintf(stderr,
                    "free-cc: too many input files "
                    "(max %d)\n", MAX_INPUTS);
                return 1;
            }
            opts->inputs[opts->num_inputs++] = argv[i];
            if (opts->input == NULL) {
                opts->input = argv[i];
            }
        }
    }

    if (opts->num_inputs == 0) {
        fprintf(stderr, "free-cc: no input file\n");
        usage();
        return 1;
    }

    return 0;
}

/* ---- pipeline stages ---- */

/*
 * stage_compile - run lexer -> preprocessor -> parser -> codegen -> optimize.
 * Reads C source from input_path, writes optimized assembly to asm_path.
 * Returns 0 on success.
 */
static int stage_compile(const char *input_path, const char *asm_path)
{
    char *src;
    long src_len;
    FILE *asm_out;
    FILE *tmp;
    char *arena_buf;
    struct arena arena;
    struct node *prog;
    char *asm_buf;
    long asm_len;
    size_t nread;
    unsigned long arena_size;

    src = read_file(input_path, &src_len);
    if (src == NULL) {
        fprintf(stderr, "free-cc: cannot read '%s'\n", input_path);
        return 1;
    }

    arena_size = cc_arena_size();
    arena_buf = arena_mmap(arena_size);
    if (arena_buf == NULL) {
        fprintf(stderr, "free-cc: out of memory\n");
        free(src);
        return 1;
    }
    arena_init(&arena, arena_buf, arena_size);

    /* parse (internally runs lex -> preprocess -> parse) */
    prog = parse(src, input_path, &arena);
    if (prog == NULL && diag_had_errors()) {
        /* real parse errors occurred */
        free(src);
        arena_munmap(arena_buf, arena_size);
        return 1;
    }

    /* check if diagnostics accumulated errors */
    if (diag_had_errors()) {
        fprintf(stderr, "free-cc: %d error(s) generated\n",
                cc_error_count);
        free(src);
        arena_munmap(arena_buf, arena_size);
        return 1;
    }

    /* build SSA IR at -O2+ for analysis, LTO, and optimized codegen */
    if (cc_opt_level >= 2) {
        struct ir_module *mod;
        mod = ir_build(prog, &arena);
        if (mod != NULL) {
            /* run optimization passes on each function */
            struct ir_func *optf;
            for (optf = mod->funcs; optf; optf = optf->next) {
                opt_mem2reg(optf);
                opt_sccp(optf, &arena);
                opt_dce(optf);
            }
            /* inter-procedural: inline small callees */
            opt_inline(mod, &arena);
            if (cc_opt_level >= 3) {
                fprintf(stderr, "free-cc: IR dump for '%s':\n",
                        input_path);
                ir_print(mod, stderr);
            }
            cc_ir_mod = mod;
        }
    }

    /* code generation to temporary buffer */
    tmp = tmpfile();
    if (tmp == NULL) {
        fprintf(stderr, "free-cc: cannot create temp file\n");
        free(src);
        arena_munmap(arena_buf, arena_size);
        return 1;
    }

    /* set source info for DWARF debug generation */
    if (cc_debug_info) {
        gen_set_source_info(input_path, ".");
    }

    if (cc_opt_level >= 2 && cc_ir_mod != NULL &&
        cc_target_arch == TARGET_AARCH64 &&
        !has_inline_asm(prog)) {
        /* optimized path: IR -> aarch64 assembly */
        ir_codegen(cc_ir_mod, tmp);
    } else if (cc_target_arch == TARGET_X86_64) {
        gen_x86(prog, tmp);
    } else {
        gen(prog, tmp);
    }

    /* read generated assembly back into memory for optimization */
    fflush(tmp);
    asm_len = ftell(tmp);
    if (asm_len <= 0) {
        fclose(tmp);
        fprintf(stderr, "free-cc: codegen produced no output\n");
        free(src);
        arena_munmap(arena_buf, arena_size);
        return 1;
    }
    fseek(tmp, 0, SEEK_SET);

    asm_buf = (char *)malloc((size_t)asm_len + 1);
    if (asm_buf == NULL) {
        fclose(tmp);
        fprintf(stderr, "free-cc: out of memory\n");
        free(src);
        arena_munmap(arena_buf, arena_size);
        return 1;
    }
    nread = fread(asm_buf, 1, (size_t)asm_len, tmp);
    fclose(tmp);
    asm_buf[nread] = '\0';

    /* peephole optimization */
    asm_len = opt_peephole(asm_buf, (int)nread);

    /* write optimized assembly to output file */
    asm_out = fopen(asm_path, "w");
    if (asm_out == NULL) {
        fprintf(stderr, "free-cc: cannot write '%s'\n", asm_path);
        free(asm_buf);
        free(src);
        arena_munmap(arena_buf, arena_size);
        return 1;
    }
    fwrite(asm_buf, 1, (size_t)asm_len, asm_out);
    fclose(asm_out);

    free(asm_buf);
    free(src);
    arena_munmap(arena_buf, arena_size);
    return 0;
}

/*
 * stage_assemble - assemble .s file to .o file.
 * Tries free-as first, then falls back to system 'as'.
 * Returns 0 on success.
 */
static int stage_assemble(const char *asm_path, const char *obj_path)
{
    char cmd[PATH_BUF * 3];
    int ret;

    /* try free-as */
    sprintf(cmd, "free-as -o %s %s 2>/dev/null", obj_path, asm_path);
    ret = run_command(cmd);
    if (ret == 0) {
        return 0;
    }

    /* fall back to system assembler */
    sprintf(cmd, "as -o %s %s", obj_path, asm_path);
    ret = run_command(cmd);
    if (ret != 0) {
        fprintf(stderr,
            "free-cc: assembly failed\n");
    }
    return ret;
}

/*
 * stage_link - link object file into executable.
 * Tries free-ld first, then falls back to system 'cc'.
 * Returns 0 on success.
 */
static int stage_link(const char *obj_path, const char *out_path)
{
    char cmd[PATH_BUF * 3];
    int ret;

    /* try free-ld */
    sprintf(cmd, "free-ld -o %s %s 2>/dev/null", out_path, obj_path);
    ret = run_command(cmd);
    if (ret == 0) {
        return 0;
    }

    /* fall back to system cc for linking (handles crt, libc, libm) */
    sprintf(cmd, "cc -no-pie -o %s %s -lm", out_path, obj_path);
    ret = run_command(cmd);
    if (ret != 0) {
        fprintf(stderr, "free-cc: linking failed\n");
    }
    return ret;
}

/*
 * stage_link_multi - link multiple object files into executable.
 * obj_paths is an array of num_objs paths.
 * Returns 0 on success.
 */
static int stage_link_multi(const char **obj_paths, int num_objs,
                            const char *out_path)
{
    char *cmd;
    int cmd_size;
    int pos;
    int oi;
    int ret;

    /* allocate buffer for command: generous size */
    cmd_size = PATH_BUF * (num_objs + 2);
    cmd = (char *)malloc((size_t)cmd_size);
    if (cmd == NULL) {
        fprintf(stderr, "free-cc: out of memory\n");
        return 1;
    }

    /* try free-ld */
    pos = sprintf(cmd, "free-ld -o %s", out_path);
    for (oi = 0; oi < num_objs; oi++) {
        pos += sprintf(cmd + pos, " %s", obj_paths[oi]);
    }
    strcat(cmd + pos, " 2>/dev/null");
    ret = run_command(cmd);
    if (ret == 0) {
        free(cmd);
        return 0;
    }

    /* fall back to system cc */
    pos = sprintf(cmd, "cc -no-pie -o %s", out_path);
    for (oi = 0; oi < num_objs; oi++) {
        pos += sprintf(cmd + pos, " %s", obj_paths[oi]);
    }
    strcat(cmd + pos, " -lm");
    ret = run_command(cmd);
    if (ret != 0) {
        fprintf(stderr, "free-cc: linking failed\n");
    }
    free(cmd);
    return ret;
}

/*
 * detect_lang_mode - determine effective language mode from
 * explicit -x setting or file extension.
 */
static int detect_lang_mode(const struct cc_opts *opts)
{
    const char *ext;
    int len;

    if (opts->lang_mode != LANG_AUTO) {
        return opts->lang_mode;
    }
    if (opts->input == NULL) {
        return LANG_C;
    }

    len = (int)strlen(opts->input);
    if (len >= 2) {
        ext = opts->input + len - 2;
        if (strcmp(ext, ".S") == 0) {
            return LANG_ASM_CPP;
        }
        if (strcmp(ext, ".s") == 0) {
            return LANG_ASM;
        }
    }
    return LANG_C;
}

/*
 * stage_preprocess_asm - preprocess a .S file for assembly.
 * Runs the preprocessor with -D, -I, -include applied,
 * writes output to out_path (or stdout if out_path is NULL).
 * Returns 0 on success.
 */
static int stage_preprocess_asm(const char *input_path,
                                const char *out_path)
{
    /* GCC defines __ASSEMBLER__ for assembler-with-cpp inputs (.S). */
    pp_add_cmdline_define("__ASSEMBLER__=1");
    return pp_preprocess_to_file(input_path, out_path);
}

/*
 * stage_invoke_assembler - invoke free-as (or fallback) on a .s file.
 * Returns 0 on success.
 */
static int stage_invoke_assembler(const char *asm_path,
                                  const char *obj_path)
{
    return stage_assemble(asm_path, obj_path);
}

/* ---- main ---- */

int main(int argc, char **argv)
{
    struct cc_opts opts;
    const char *asm_path;
    const char *obj_path;
    const char *out_path;
    char asm_buf[PATH_BUF];
    char obj_buf[PATH_BUF];
    int ret;
    int asm_is_tmp;
    int obj_is_tmp;

    /* initialize diagnostic system before arg parsing */
    diag_init();

    /*
     * Handle probing flags that print info and exit immediately.
     * Kbuild's cc-option checks use these; they must work even
     * without an input file.
     */
    {
        int pi;
        int verbose_flag = 0;

        for (pi = 1; pi < argc; pi++) {
            if (strcmp(argv[pi], "--version") == 0) {
                printf("free-cc (free) 14.1.0\n"
                       "Target: aarch64-linux-gnu\n");
                return 0;
            }
            if (strcmp(argv[pi], "-dumpversion") == 0) {
                printf("4.0.0\n");
                return 0;
            }
            if (strcmp(argv[pi], "-dumpmachine") == 0) {
                printf("aarch64-linux-gnu\n");
                return 0;
            }
            if (strcmp(argv[pi], "-dumpspecs") == 0) {
                return 0;
            }
            if (strncmp(argv[pi], "-print-file-name=", 17) == 0) {
                /*
                 * Print the path to the requested file. For now,
                 * just echo the name back (GCC does this for
                 * unknown files). Kbuild uses this to find
                 * include directories.
                 */
                printf("%s\n", argv[pi] + 17);
                return 0;
            }
            if (strcmp(argv[pi], "-v") == 0) {
                verbose_flag = 1;
            }
        }

        if (verbose_flag) {
            fprintf(stderr, "free-cc version 0.1.0\n"
                            "Target: aarch64-linux-gnu\n");
        }
        (void)verbose_flag;
    }

    if (parse_args(argc, argv, &opts) != 0) {
        return 1;
    }

    /* Handle stdin input: read stdin into a temp file */
    if (opts.input != NULL && strcmp(opts.input, "-") == 0) {
        const char *stdin_path;
        int si;
        stdin_path = read_stdin_to_tmpfile();
        if (stdin_path == NULL) {
            return 1;
        }
        opts.input = stdin_path;
        for (si = 0; si < opts.num_inputs; si++) {
            if (strcmp(opts.inputs[si], "-") == 0) {
                opts.inputs[si] = stdin_path;
            }
        }
    }

    /* apply -w from opts to diag system */
    if (opts.suppress_warnings) {
        cc_suppress_warnings = 1;
    }

    /* initialize language standard feature flags */
    cc_std_init(opts.std_level);

    /* set target architecture */
    cc_target_arch = opts.target;

    /* set debug info generation */
    cc_debug_info = opts.debug_info;

    /* set compiler flags from options */
    cc_freestanding = opts.freestanding;
    cc_nostdinc = opts.nostdinc;
    cc_function_sections = opts.func_sections;
    cc_data_sections = opts.data_sections;
    cc_general_regs_only = opts.general_regs_only;
    cc_no_builtin = opts.no_builtin;
    cc_omit_frame_pointer = opts.omit_frame_pointer;

    /* -ffreestanding implies -fno-builtin */
    if (cc_freestanding) {
        cc_no_builtin = 1;
    }

    /* -ffreestanding implies -nostdinc for system headers */
    if (cc_freestanding) {
        cc_nostdinc = 1;
    }

    /* Match GCC/Clang: optimized builds define __OPTIMIZE__. */
    if (cc_opt_level > 0 || opts.opt_debug) {
        pp_add_cmdline_define("__OPTIMIZE__=1");
    }
    if (opts.opt_size) {
        pp_add_cmdline_define("__OPTIMIZE_SIZE__=1");
    }
    if (cc_opt_level == 0 && !opts.opt_debug) {
        pp_add_cmdline_define("__NO_INLINE__=1");
    }

    asm_is_tmp = 0;
    obj_is_tmp = 0;

    /*
     * Reject -o with -c or -S when multiple input files given,
     * since the output would be ambiguous.
     */
    if (opts.num_inputs > 1 && opts.output != NULL &&
        (opts.stop_after_obj || opts.stop_after_asm)) {
        fprintf(stderr,
            "free-cc: cannot specify -o with -c or -S "
            "with multiple files\n");
        return 1;
    }

    /*
     * Determine output paths for each pipeline stage.
     * (For multi-file builds, these are only used in the
     *  single-file fallback path below.)
     */

    /* assembly output */
    if (opts.stop_after_asm) {
        if (opts.output != NULL) {
            asm_path = opts.output;
        } else {
            asm_path = replace_ext(opts.input, ".s");
        }
    } else {
        strcpy(asm_buf, make_tmppath(".s"));
        asm_path = asm_buf;
        asm_is_tmp = 1;
    }

    /* object output */
    if (opts.stop_after_obj) {
        if (opts.output != NULL) {
            obj_path = opts.output;
        } else {
            obj_path = replace_ext(opts.input, ".o");
        }
    } else if (!opts.stop_after_asm) {
        strcpy(obj_buf, make_tmppath(".o"));
        obj_path = obj_buf;
        obj_is_tmp = 1;
    } else {
        obj_path = NULL;
    }

    /* final executable output */
    if (!opts.stop_after_asm && !opts.stop_after_obj) {
        if (opts.output != NULL) {
            out_path = opts.output;
        } else {
            out_path = replace_ext(opts.input, "");
            if (out_path[0] == '\0') {
                out_path = "a.out";
            }
        }
    } else {
        out_path = NULL;
    }

    /* Register include paths with preprocessor */
    {
        int ii;
        for (ii = 0; ii < opts.num_include_paths; ii++) {
            pp_add_include_path(opts.include_paths[ii]);
        }
    }

    /* Add default system include paths unless -nostdinc */
    if (!cc_nostdinc) {
        static char self_inc[PATH_BUF];
        const char *slash;
        int dirlen;

        /* Try to find libc/include relative to the compiler binary.
         * argv[0] is something like "/path/to/build/free-cc" or
         * "./build/free-cc".  We look for ../src/libc/include
         * relative to the directory containing the binary. */
        slash = strrchr(argv[0], '/');
        if (slash != NULL) {
            dirlen = (int)(slash - argv[0]);
            if (dirlen + 30 < PATH_BUF) {
                /* try <bindir>/../src/libc/include */
                sprintf(self_inc, "%.*s/../src/libc/include",
                        dirlen, argv[0]);
                if (access(self_inc, R_OK) == 0) {
                    pp_add_include_path(self_inc);
                }
            }
        }

        /* System include paths */
        pp_add_include_path("/usr/local/include");
        pp_add_include_path("/usr/include");
        /* aarch64-specific multiarch path */
        pp_add_include_path(
            "/usr/include/aarch64-linux-gnu");
    }

    /* Register -D defines (stored now, applied during pp_init) */
    {
        int ii;
        for (ii = 0; ii < opts.num_defines; ii++) {
            pp_add_cmdline_define(opts.defines[ii]);
        }
    }

    /* Register -U undefs (stored now, applied during pp_init) */
    {
        int ii;
        for (ii = 0; ii < opts.num_undefs; ii++) {
            pp_add_cmdline_undef(opts.undefs[ii]);
        }
    }

    /* Register -include force-includes (stored, applied during pp_init) */
    {
        int ii;
        for (ii = 0; ii < opts.num_force_includes; ii++) {
            pp_add_force_include(opts.force_includes[ii]);
        }
    }

    /* Configure dependency tracking for -MD/-MMD */
    if (opts.gen_deps) {
        pp_dep_set_exclude_system(opts.gen_deps_nosys);
    }

    /*
     * Handle .S and .s assembly files.
     * .S: preprocess then assemble.
     * .s: assemble directly (no preprocessing).
     */
    {
        int lang = detect_lang_mode(&opts);

        if (lang == LANG_ASM_CPP) {
            /* .S file: preprocess, then assemble */
            if (opts.stop_after_pp) {
                /* -E with .S: just preprocess to stdout */
                ret = stage_preprocess_asm(opts.input, opts.output);
                if (ret == 0 && opts.gen_deps) {
                    static char dep_path_buf[PATH_BUF];
                    static char dep_targ_buf[PATH_BUF];
                    const char *df;
                    const char *dt;

                    if (opts.dep_file != NULL) {
                        df = opts.dep_file;
                    } else {
                        strcpy(dep_path_buf,
                               replace_ext(opts.input, ".d"));
                        df = dep_path_buf;
                    }
                    if (opts.dep_target != NULL) {
                        dt = opts.dep_target;
                    } else if (opts.output != NULL) {
                        dt = opts.output;
                    } else {
                        strcpy(dep_targ_buf,
                               replace_ext(opts.input, ".o"));
                        dt = dep_targ_buf;
                    }
                    write_depfile(df, dt);
                }
                return ret;
            }

            /* preprocess to temp .s file, then assemble */
            {
                const char *pp_path;
                const char *final_obj;
                char pp_buf[PATH_BUF];
                static char dep_path_buf[PATH_BUF];
                static char dep_targ_buf[PATH_BUF];
                const char *df;
                const char *dt;

                strcpy(pp_buf, make_tmppath(".s"));
                pp_path = pp_buf;

                ret = stage_preprocess_asm(opts.input, pp_path);
                if (ret != 0) {
                    remove(pp_path);
                    return ret;
                }

                /* determine object output path */
                if (opts.stop_after_obj || opts.stop_after_asm) {
                    if (opts.output != NULL) {
                        final_obj = opts.output;
                    } else {
                        final_obj = replace_ext(opts.input, ".o");
                    }
                } else {
                    final_obj = replace_ext(opts.input, ".o");
                }

                ret = stage_invoke_assembler(pp_path, final_obj);
                remove(pp_path);
                if (ret == 0 && opts.gen_deps) {
                    if (opts.dep_file != NULL) {
                        df = opts.dep_file;
                    } else {
                        strcpy(dep_path_buf,
                               replace_ext(opts.input, ".d"));
                        df = dep_path_buf;
                    }
                    if (opts.dep_target != NULL) {
                        dt = opts.dep_target;
                    } else if (opts.output != NULL) {
                        dt = opts.output;
                    } else {
                        dt = final_obj;
                        if (dt == NULL) {
                            strcpy(dep_targ_buf,
                                   replace_ext(opts.input, ".o"));
                            dt = dep_targ_buf;
                        }
                    }
                    write_depfile(df, dt);
                }
                return ret;
            }
        }

        if (lang == LANG_ASM) {
            /* .s file: assemble directly, no preprocessing */
            const char *final_obj;

            if (opts.stop_after_pp) {
                /* -E on plain .s: nothing to preprocess, just cat */
                char *src;
                src = read_file(opts.input, NULL);
                if (src == NULL) {
                    fprintf(stderr, "free-cc: cannot read '%s'\n",
                            opts.input);
                    return 1;
                }
                printf("%s", src);
                free(src);
                return 0;
            }

            if (opts.output != NULL) {
                final_obj = opts.output;
            } else {
                final_obj = replace_ext(opts.input, ".o");
            }

            return stage_invoke_assembler(opts.input, final_obj);
        }
    }

    /* Stage 0: preprocess only (-E) */
    if (opts.stop_after_pp) {
        ret = pp_preprocess_to_file(opts.input, opts.output);
        if (ret == 0 && opts.gen_deps) {
            const char *df;
            const char *dt;
            static char dep_path_buf[PATH_BUF];
            static char dep_targ_buf[PATH_BUF];

            /* determine dep file path (copy - replace_ext
             * uses a static buffer) */
            if (opts.dep_file != NULL) {
                df = opts.dep_file;
            } else {
                strcpy(dep_path_buf,
                       replace_ext(opts.input, ".d"));
                df = dep_path_buf;
            }
            /* determine dep target */
            if (opts.dep_target != NULL) {
                dt = opts.dep_target;
            } else if (opts.output != NULL) {
                dt = opts.output;
            } else {
                strcpy(dep_targ_buf,
                       replace_ext(opts.input, ".o"));
                dt = dep_targ_buf;
            }
            write_depfile(df, dt);
        }
        return ret;
    }

    /*
     * Multi-file compilation: compile each .c to .o, then link.
     * For -c or -S with multiple files, produce per-file outputs.
     */
    if (opts.num_inputs > 1) {
        const char *tmp_objs[MAX_INPUTS];
        char (*tmp_obj_bufs)[PATH_BUF];
        char cur_asm_buf[PATH_BUF];
        int fi;

        tmp_obj_bufs = (char (*)[PATH_BUF])malloc(
            (size_t)opts.num_inputs * PATH_BUF);
        if (tmp_obj_bufs == NULL) {
            fprintf(stderr, "free-cc: out of memory\n");
            return 1;
        }

        /* compile each input file */
        for (fi = 0; fi < opts.num_inputs; fi++) {
            const char *cur_input;
            const char *cur_asm;
            const char *cur_obj;

            cur_input = opts.inputs[fi];

            /* -S mode: produce .s for each file */
            if (opts.stop_after_asm) {
                cur_asm = replace_ext(cur_input, ".s");
                ret = stage_compile(cur_input, cur_asm);
                if (ret != 0) {
                    free(tmp_obj_bufs);
                    return ret;
                }
                continue;
            }

            /* compile to temp .s */
            strcpy(cur_asm_buf, make_tmppath(".s"));
            cur_asm = cur_asm_buf;
            ret = stage_compile(cur_input, cur_asm);
            if (ret != 0) {
                /* clean up tmp .o files already created */
                int ci;
                for (ci = 0; ci < fi; ci++) {
                    if (!opts.stop_after_obj) {
                        remove(tmp_objs[ci]);
                    }
                }
                remove(cur_asm);
                free(tmp_obj_bufs);
                return ret;
            }

            /* -c mode: produce .o for each file */
            if (opts.stop_after_obj) {
                cur_obj = replace_ext(cur_input, ".o");
            } else {
                /* temp .o for linking */
                strcpy(tmp_obj_bufs[fi],
                       make_tmppath(".o"));
                cur_obj = tmp_obj_bufs[fi];
            }
            tmp_objs[fi] = cur_obj;

            /* assemble .s -> .o */
            ret = stage_assemble(cur_asm, cur_obj);
            remove(cur_asm);
            if (ret != 0) {
                /* clean up tmp .o files */
                int ci;
                for (ci = 0; ci < fi; ci++) {
                    if (!opts.stop_after_obj) {
                        remove(tmp_objs[ci]);
                    }
                }
                free(tmp_obj_bufs);
                return ret;
            }

            /* LTO embed if requested */
            if (opts.enable_lto && cc_ir_mod != NULL) {
                if (ir_write_to_elf(cur_obj,
                                    cc_ir_mod) != 0) {
                    fprintf(stderr,
                        "free-cc: warning: could not "
                        "embed LTO IR\n");
                }
            }
        }

        /* -S or -c: done, no linking needed */
        if (opts.stop_after_asm || opts.stop_after_obj) {
            free(tmp_obj_bufs);
            return 0;
        }

        /* link all .o files into final executable */
        ret = stage_link_multi(tmp_objs, opts.num_inputs,
                               out_path);

        /* clean up temp .o files */
        for (fi = 0; fi < opts.num_inputs; fi++) {
            remove(tmp_objs[fi]);
        }

        free(tmp_obj_bufs);
        return ret;
    }

    /* --- Single-file path (original behavior) --- */

    /* Stage 1: compile C source to assembly */
    ret = stage_compile(opts.input, asm_path);
    if (ret != 0) {
        return ret;
    }

    /* Write dependency file after successful compilation */
    if (opts.gen_deps) {
        static char dep_path_buf2[PATH_BUF];
        static char dep_targ_buf2[PATH_BUF];
        const char *df;
        const char *dt;
        FILE *depout;

        /* determine dep file path (copy - replace_ext
         * uses a static buffer) */
        if (opts.dep_file != NULL) {
            df = opts.dep_file;
        } else {
            strcpy(dep_path_buf2,
                   replace_ext(opts.input, ".d"));
            df = dep_path_buf2;
        }
        /* determine dep target */
        if (opts.dep_target != NULL) {
            dt = opts.dep_target;
        } else if (opts.output != NULL) {
            dt = opts.output;
        } else {
            strcpy(dep_targ_buf2,
                   replace_ext(opts.input, ".o"));
            dt = dep_targ_buf2;
        }
        depout = fopen(df, "w");
        if (depout != NULL) {
            pp_dep_write(depout, dt);
            fclose(depout);
        }
    }

    if (opts.stop_after_asm) {
        return 0;
    }

    /* Stage 2: assemble to object file */
    ret = stage_assemble(asm_path, obj_path);
    if (asm_is_tmp) {
        remove(asm_path);
    }
    if (ret != 0) {
        return ret;
    }

    /*
     * Stage 2b: if -flto, serialize the IR module into a .free_ir
     * section in the .o file. The linker detects this and runs
     * whole-program optimization at link time.
     */
    if (opts.enable_lto && obj_path != NULL) {
        if (cc_ir_mod != NULL) {
            if (ir_write_to_elf(obj_path, cc_ir_mod) != 0) {
                fprintf(stderr,
                    "free-cc: warning: could not embed LTO IR\n");
            }
        } else {
            /* no IR built (opt < 2); create empty module for LTO */
            char *lbuf;
            struct arena la;
            struct ir_module *stub;
            unsigned long lto_arena_size;

            lto_arena_size = cc_arena_size();
            lbuf = arena_mmap(lto_arena_size);
            if (lbuf != NULL) {
                arena_init(&la, lbuf, lto_arena_size);
                stub = (struct ir_module *)arena_alloc(
                    &la, sizeof(struct ir_module));
                memset(stub, 0, sizeof(*stub));
                stub->arena = &la;
                if (ir_write_to_elf(obj_path, stub) != 0) {
                    fprintf(stderr,
                        "free-cc: warning: could not embed "
                        "LTO IR\n");
                }
                arena_munmap(lbuf, lto_arena_size);
            }
        }
    }

    if (opts.stop_after_obj) {
        return 0;
    }

    /* Stage 3: link to executable */
    ret = stage_link(obj_path, out_path);
    if (obj_is_tmp) {
        remove(obj_path);
    }

    return ret;
}
