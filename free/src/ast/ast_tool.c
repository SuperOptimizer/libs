/*
 * ast_tool.c - AST dump tool for the free toolchain
 * Usage: free-ast [-D name[=val]] [-I dir] [--flat] input.c
 * Parses a C file and prints the AST tree.
 * Pure C89. Wraps lex.c + pp.c + parse.c + type.c.
 */

#include "free.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- compiler interfaces ---- */
extern struct node *parse(const char *src, const char *filename,
                          struct arena *a);
extern void pp_add_include_path(const char *path);
extern void pp_add_cmdline_define(const char *def);
extern void pp_add_cmdline_undef(const char *name);
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

/* ---- node kind name ---- */
static const char *node_kind_name(enum node_kind k)
{
    switch (k) {
    case ND_NUM:        return "NUM";
    case ND_FNUM:       return "FNUM";
    case ND_VAR:        return "VAR";
    case ND_STR:        return "STR";
    case ND_ADD:        return "ADD";
    case ND_SUB:        return "SUB";
    case ND_MUL:        return "MUL";
    case ND_DIV:        return "DIV";
    case ND_MOD:        return "MOD";
    case ND_BITAND:     return "BITAND";
    case ND_BITOR:      return "BITOR";
    case ND_BITXOR:     return "BITXOR";
    case ND_BITNOT:     return "BITNOT";
    case ND_SHL:        return "SHL";
    case ND_SHR:        return "SHR";
    case ND_LOGAND:     return "LOGAND";
    case ND_LOGOR:      return "LOGOR";
    case ND_LOGNOT:     return "LOGNOT";
    case ND_EQ:         return "EQ";
    case ND_NE:         return "NE";
    case ND_LT:         return "LT";
    case ND_LE:         return "LE";
    case ND_ASSIGN:     return "ASSIGN";
    case ND_ADDR:       return "ADDR";
    case ND_DEREF:      return "DEREF";
    case ND_RETURN:     return "RETURN";
    case ND_IF:         return "IF";
    case ND_WHILE:      return "WHILE";
    case ND_FOR:        return "FOR";
    case ND_DO:         return "DO";
    case ND_BLOCK:      return "BLOCK";
    case ND_CALL:       return "CALL";
    case ND_CAST:       return "CAST";
    case ND_COMMA_EXPR: return "COMMA_EXPR";
    case ND_MEMBER:     return "MEMBER";
    case ND_TERNARY:    return "TERNARY";
    case ND_SWITCH:     return "SWITCH";
    case ND_CASE:       return "CASE";
    case ND_BREAK:      return "BREAK";
    case ND_CONTINUE:   return "CONTINUE";
    case ND_GOTO:       return "GOTO";
    case ND_LABEL:      return "LABEL";
    case ND_PRE_INC:    return "PRE_INC";
    case ND_PRE_DEC:    return "PRE_DEC";
    case ND_POST_INC:   return "POST_INC";
    case ND_POST_DEC:   return "POST_DEC";
    case ND_FUNCDEF:    return "FUNCDEF";
    case ND_VLA_ALLOC:  return "VLA_ALLOC";
    case ND_COMPOUND_LIT: return "COMPOUND_LIT";
    case ND_DESIG_INIT: return "DESIG_INIT";
    case ND_BOOL_CONV:  return "BOOL_CONV";
    case ND_STATIC_ASSERT: return "STATIC_ASSERT";
    case ND_GENERIC:    return "GENERIC";
    case ND_NULLPTR:    return "NULLPTR";
    case ND_INIT_LIST:  return "INIT_LIST";
    case ND_VA_START:   return "VA_START";
    case ND_VA_ARG:     return "VA_ARG";
    case ND_STMT_EXPR:  return "STMT_EXPR";
    case ND_LABEL_ADDR: return "LABEL_ADDR";
    case ND_GOTO_INDIRECT: return "GOTO_INDIRECT";
    case ND_BUILTIN_OVERFLOW: return "BUILTIN_OVERFLOW";
    default:            return "UNKNOWN";
    }
}

/* ---- type name ---- */
static const char *type_kind_str(enum type_kind k)
{
    switch (k) {
    case TY_VOID:   return "void";
    case TY_CHAR:   return "char";
    case TY_SHORT:  return "short";
    case TY_INT:    return "int";
    case TY_LONG:   return "long";
    case TY_FLOAT:  return "float";
    case TY_DOUBLE: return "double";
    case TY_PTR:    return "ptr";
    case TY_ARRAY:  return "array";
    case TY_STRUCT: return "struct";
    case TY_UNION:  return "union";
    case TY_ENUM:   return "enum";
    case TY_FUNC:   return "func";
    case TY_BOOL:   return "_Bool";
    case TY_LLONG:  return "long long";
    case TY_ATOMIC: return "_Atomic";
    default:        return "?";
    }
}

/* ---- AST printer ---- */
static int flat_mode;

static void print_indent(int depth)
{
    int i;

    if (flat_mode) {
        return;
    }
    for (i = 0; i < depth * 2; i++) {
        putchar(' ');
    }
}

static void dump_node(struct node *n, int depth)
{
    struct node *child;

    if (n == NULL) {
        return;
    }

    print_indent(depth);
    printf("%s", node_kind_name(n->kind));

    /* type info */
    if (n->ty != NULL) {
        printf(" <%s", type_kind_str(n->ty->kind));
        if (n->ty->is_unsigned) {
            printf(" unsigned");
        }
        printf(">");
    }

    /* value info */
    if (n->kind == ND_NUM) {
        printf(" %ld", n->val);
    } else if (n->kind == ND_FNUM) {
        printf(" %g", n->fval);
    } else if (n->kind == ND_VAR && n->name != NULL) {
        printf(" '%s'", n->name);
    } else if (n->kind == ND_STR && n->name != NULL) {
        printf(" \"%s\"", n->name);
    } else if (n->kind == ND_FUNCDEF && n->name != NULL) {
        printf(" '%s'", n->name);
    } else if (n->kind == ND_CALL && n->name != NULL) {
        printf(" '%s'", n->name);
    } else if (n->kind == ND_LABEL && n->name != NULL) {
        printf(" '%s'", n->name);
    } else if (n->kind == ND_GOTO && n->name != NULL) {
        printf(" '%s'", n->name);
    } else if (n->kind == ND_MEMBER && n->name != NULL) {
        printf(" .%s", n->name);
    }

    printf("\n");

    /* children */
    if (n->lhs != NULL) {
        dump_node(n->lhs, depth + 1);
    }
    if (n->rhs != NULL) {
        dump_node(n->rhs, depth + 1);
    }
    if (n->cond != NULL) {
        print_indent(depth + 1);
        printf("[cond]\n");
        dump_node(n->cond, depth + 2);
    }
    if (n->then_ != NULL) {
        print_indent(depth + 1);
        printf("[then]\n");
        dump_node(n->then_, depth + 2);
    }
    if (n->els != NULL) {
        print_indent(depth + 1);
        printf("[else]\n");
        dump_node(n->els, depth + 2);
    }
    if (n->init != NULL) {
        print_indent(depth + 1);
        printf("[init]\n");
        dump_node(n->init, depth + 2);
    }
    if (n->inc != NULL) {
        print_indent(depth + 1);
        printf("[inc]\n");
        dump_node(n->inc, depth + 2);
    }
    if (n->body != NULL) {
        child = n->body;
        while (child != NULL) {
            dump_node(child, depth + 1);
            child = child->next;
        }
    }
    if (n->args != NULL) {
        child = n->args;
        print_indent(depth + 1);
        printf("[args]\n");
        while (child != NULL) {
            dump_node(child, depth + 2);
            child = child->next;
        }
    }
}

/* ---- constants ---- */
#define ARENA_SIZE (64 * 1024 * 1024)

/* ---- usage ---- */
static void usage(void)
{
    fprintf(stderr,
        "Usage: free-ast [options] <input.c>\n"
        "Options:\n"
        "  -D <name>[=val]  Define macro\n"
        "  -I <dir>         Add include path\n"
        "  --flat           Flat output (no indentation)\n"
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
    struct node *prog;
    struct node *fn;

    input = NULL;
    flat_mode = 0;
    cc_std_init(STD_C89);

    /* Handle --version before full arg parsing */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("free-ast (free) 0.1.0\n");
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
        } else if (strcmp(argv[i], "--flat") == 0) {
            flat_mode = 1;
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "free-ast: unknown option: %s\n", argv[i]);
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
        fprintf(stderr, "free-ast: cannot open '%s'\n", input);
        return 1;
    }
    fseek(inf, 0, SEEK_END);
    sz = ftell(inf);
    fseek(inf, 0, SEEK_SET);

    arena_buf = (char *)malloc(ARENA_SIZE);
    if (arena_buf == NULL) {
        fprintf(stderr, "free-ast: out of memory\n");
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

    /* parse */
    prog = parse(src, input, &arena);

    /* dump all top-level nodes */
    fn = prog;
    while (fn != NULL) {
        dump_node(fn, 0);
        fn = fn->next;
    }

    free(arena_buf);
    return 0;
}
