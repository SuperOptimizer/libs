/*
 * lex_tool.c - Token dump tool for the free toolchain
 * Usage: free-lex [-D name[=val]] [-I dir] input.c
 * Tokenizes a C file and prints each token.
 * Pure C89. Wraps lex.c + pp.c.
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

/* ---- diagnostics (from diag.c) ---- */
extern void diag_init(void);

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

/* ---- token kind name table ---- */
static const char *tok_kind_name(enum tok_kind k)
{
    switch (k) {
    case TOK_NUM:       return "NUM";
    case TOK_FNUM:      return "FNUM";
    case TOK_IDENT:     return "IDENT";
    case TOK_STR:       return "STR";
    case TOK_CHAR_LIT:  return "CHAR";
    case TOK_PLUS:      return "PLUS";
    case TOK_MINUS:     return "MINUS";
    case TOK_STAR:      return "STAR";
    case TOK_SLASH:     return "SLASH";
    case TOK_PERCENT:   return "PERCENT";
    case TOK_AMP:       return "AMP";
    case TOK_PIPE:      return "PIPE";
    case TOK_CARET:     return "CARET";
    case TOK_TILDE:     return "TILDE";
    case TOK_LSHIFT:    return "LSHIFT";
    case TOK_RSHIFT:    return "RSHIFT";
    case TOK_AND:       return "AND";
    case TOK_OR:        return "OR";
    case TOK_NOT:       return "NOT";
    case TOK_EQ:        return "EQ";
    case TOK_NE:        return "NE";
    case TOK_LT:        return "LT";
    case TOK_GT:        return "GT";
    case TOK_LE:        return "LE";
    case TOK_GE:        return "GE";
    case TOK_ASSIGN:    return "ASSIGN";
    case TOK_PLUS_EQ:   return "PLUS_EQ";
    case TOK_MINUS_EQ:  return "MINUS_EQ";
    case TOK_STAR_EQ:   return "STAR_EQ";
    case TOK_SLASH_EQ:  return "SLASH_EQ";
    case TOK_PERCENT_EQ: return "PERCENT_EQ";
    case TOK_AMP_EQ:    return "AMP_EQ";
    case TOK_PIPE_EQ:   return "PIPE_EQ";
    case TOK_CARET_EQ:  return "CARET_EQ";
    case TOK_LSHIFT_EQ: return "LSHIFT_EQ";
    case TOK_RSHIFT_EQ: return "RSHIFT_EQ";
    case TOK_SEMI:      return "SEMI";
    case TOK_COMMA:     return "COMMA";
    case TOK_DOT:       return "DOT";
    case TOK_ARROW:     return "ARROW";
    case TOK_ELLIPSIS:  return "ELLIPSIS";
    case TOK_LPAREN:    return "LPAREN";
    case TOK_RPAREN:    return "RPAREN";
    case TOK_LBRACE:    return "LBRACE";
    case TOK_RBRACE:    return "RBRACE";
    case TOK_LBRACKET:  return "LBRACKET";
    case TOK_RBRACKET:  return "RBRACKET";
    case TOK_QUESTION:  return "QUESTION";
    case TOK_COLON:     return "COLON";
    case TOK_HASH:      return "HASH";
    case TOK_PASTE:     return "PASTE";
    case TOK_INC:       return "INC";
    case TOK_DEC:       return "DEC";
    /* C89 keywords */
    case TOK_AUTO:      return "AUTO";
    case TOK_BREAK:     return "BREAK";
    case TOK_CASE:      return "CASE";
    case TOK_CHAR_KW:   return "CHAR";
    case TOK_CONST:     return "CONST";
    case TOK_CONTINUE:  return "CONTINUE";
    case TOK_DEFAULT:   return "DEFAULT";
    case TOK_DO:        return "DO";
    case TOK_DOUBLE:    return "DOUBLE";
    case TOK_ELSE:      return "ELSE";
    case TOK_ENUM:      return "ENUM";
    case TOK_EXTERN:    return "EXTERN";
    case TOK_FLOAT:     return "FLOAT";
    case TOK_FOR:       return "FOR";
    case TOK_GOTO:      return "GOTO";
    case TOK_IF:        return "IF";
    case TOK_INT:       return "INT";
    case TOK_LONG:      return "LONG";
    case TOK_REGISTER:  return "REGISTER";
    case TOK_RETURN:    return "RETURN";
    case TOK_SHORT:     return "SHORT";
    case TOK_SIGNED:    return "SIGNED";
    case TOK_SIZEOF:    return "SIZEOF";
    case TOK_STATIC:    return "STATIC";
    case TOK_STRUCT:    return "STRUCT";
    case TOK_SWITCH:    return "SWITCH";
    case TOK_TYPEDEF:   return "TYPEDEF";
    case TOK_UNION:     return "UNION";
    case TOK_UNSIGNED:  return "UNSIGNED";
    case TOK_VOID:      return "VOID";
    case TOK_VOLATILE:  return "VOLATILE";
    case TOK_WHILE:     return "WHILE";
    /* C99+ keywords */
    case TOK_BOOL:      return "BOOL";
    case TOK_RESTRICT:  return "RESTRICT";
    case TOK_INLINE:    return "INLINE";
    case TOK_ALIGNAS:   return "ALIGNAS";
    case TOK_ALIGNOF:   return "ALIGNOF";
    case TOK_STATIC_ASSERT: return "STATIC_ASSERT";
    case TOK_NORETURN:  return "NORETURN";
    case TOK_GENERIC:   return "GENERIC";
    case TOK_ATOMIC:    return "ATOMIC";
    case TOK_THREAD_LOCAL: return "THREAD_LOCAL";
    case TOK_TRUE:      return "TRUE";
    case TOK_FALSE:     return "FALSE";
    case TOK_BOOL_KW:   return "BOOL_KW";
    case TOK_NULLPTR:   return "NULLPTR";
    case TOK_TYPEOF:    return "TYPEOF";
    case TOK_TYPEOF_UNQUAL: return "TYPEOF_UNQUAL";
    case TOK_CONSTEXPR: return "CONSTEXPR";
    case TOK_STATIC_ASSERT_KW: return "STATIC_ASSERT_KW";
    case TOK_ATTR_OPEN: return "ATTR_OPEN";
    case TOK_ATTR_CLOSE: return "ATTR_CLOSE";
    case TOK_EOF:       return "EOF";
    default:            return "UNKNOWN";
    }
}

/* ---- constants ---- */
#define ARENA_SIZE (64 * 1024 * 1024)

/* ---- usage ---- */
static void usage(void)
{
    fprintf(stderr,
        "Usage: free-lex [options] <input.c>\n"
        "Options:\n"
        "  -D <name>[=val]  Define macro\n"
        "  -I <dir>         Add include path\n"
        "  -h               Show this help\n"
    );
}

/* ---- main ---- */
int main(int argc, char **argv)
{
    const char *input;
    int i;
    FILE *inf;
    long sz;
    char *src;
    size_t nread;
    char *arena_buf;
    struct arena arena;
    struct tok *t;

    input = NULL;
    cc_std_init(STD_C89);

    /* Handle --version before full arg parsing */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("free-lex (free) 0.1.0\n");
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
        } else if (strcmp(argv[i], "-I") == 0 && i + 1 < argc) {
            i++;
            pp_add_include_path(argv[i]);
        } else if (strncmp(argv[i], "-I", 2) == 0 && argv[i][2] != '\0') {
            pp_add_include_path(argv[i] + 2);
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "free-lex: unknown option: %s\n", argv[i]);
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

    diag_init();

    /* read input file */
    inf = fopen(input, "rb");
    if (inf == NULL) {
        fprintf(stderr, "free-lex: cannot open '%s'\n", input);
        return 1;
    }
    fseek(inf, 0, SEEK_END);
    sz = ftell(inf);
    fseek(inf, 0, SEEK_SET);

    arena_buf = (char *)malloc(ARENA_SIZE);
    if (arena_buf == NULL) {
        fprintf(stderr, "free-lex: out of memory\n");
        fclose(inf);
        return 1;
    }
    arena_init(&arena, arena_buf, ARENA_SIZE);

    src = (char *)arena_alloc(&arena, (usize)(sz + 2));
    nread = fread(src, 1, (size_t)sz, inf);
    fclose(inf);
    if (nread > 0 && src[nread - 1] != '\n') {
        src[nread] = '\n';
        nread++;
    }
    src[nread] = '\0';

    pp_init(src, input, &arena);

    /* dump tokens */
    for (;;) {
        t = pp_next();
        if (t->kind == TOK_EOF) {
            break;
        }

        /* file:line:col KIND "text" [val=N] */
        printf("%s:%d:%d %s",
               t->file ? t->file : "<unknown>",
               t->line, t->col,
               tok_kind_name(t->kind));

        if (t->str != NULL) {
            printf(" \"%s\"", t->str);
        }

        if (t->kind == TOK_NUM) {
            printf(" [val=%ld]", t->val);
        } else if (t->kind == TOK_FNUM) {
            printf(" [val=%g]", t->fval);
        } else if (t->kind == TOK_CHAR_LIT) {
            printf(" [val=%ld]", t->val);
        }

        printf("\n");
    }

    free(arena_buf);
    return 0;
}
