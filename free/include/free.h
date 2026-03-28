/*
 * free.h - Common types, macros, and memory for the free toolchain
 * Pure C89, no external dependencies
 */
#ifndef FREE_H
#define FREE_H

/* ---- basic types (freestanding) ---- */
typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long      u64;
typedef signed char        i8;
typedef signed short       i16;
typedef signed int         i32;
typedef signed long        i64;
typedef u64                usize;
typedef i64                isize;

#ifndef NULL
#define NULL ((void*)0)
#endif

/* ---- language standard levels ---- */
#define STD_C89     0
#define STD_C99     1
#define STD_C11     2
#define STD_C23     3
#define STD_GNU89   4
#define STD_GNU99   5
#define STD_GNU11   6
#define STD_GNU23   7

/* ---- feature flags (bitmask) ---- */
#define FEAT_LINE_COMMENTS     0x00000001UL  /* // comments */
#define FEAT_LONG_LONG         0x00000002UL  /* long long type */
#define FEAT_HEX_FLOAT         0x00000004UL  /* 0x1.0p10 literals */
#define FEAT_BOOL              0x00000008UL  /* _Bool keyword */
#define FEAT_RESTRICT          0x00000010UL  /* restrict keyword */
#define FEAT_INLINE            0x00000020UL  /* inline keyword */
#define FEAT_UCN               0x00000040UL  /* universal char names */
#define FEAT_MIXED_DECL        0x00000080UL  /* decls after stmts */
#define FEAT_FOR_DECL          0x00000100UL  /* for(int i=0;...) */
#define FEAT_VLA               0x00000200UL  /* variable-length arrays */
#define FEAT_DESIG_INIT        0x00000400UL  /* designated initializers */
#define FEAT_COMPOUND_LIT      0x00000800UL  /* compound literals */
#define FEAT_FLEX_ARRAY        0x00001000UL  /* flexible array members */
#define FEAT_STATIC_ARRAY      0x00002000UL  /* static in array params */
#define FEAT_VARIADIC_MACRO    0x00004000UL  /* variadic macros */
#define FEAT_PRAGMA_OP         0x00008000UL  /* _Pragma operator */
#define FEAT_FUNC_MACRO        0x00010000UL  /* __func__ */
#define FEAT_EMPTY_MACRO_ARG   0x00020000UL  /* empty macro arguments */
#define FEAT_ALIGNAS           0x00040000UL  /* _Alignas */
#define FEAT_ALIGNOF           0x00080000UL  /* _Alignof */
#define FEAT_STATIC_ASSERT     0x00100000UL  /* _Static_assert */
#define FEAT_NORETURN          0x00200000UL  /* _Noreturn */
#define FEAT_GENERIC           0x00400000UL  /* _Generic */
#define FEAT_ATOMIC            0x00800000UL  /* _Atomic */
#define FEAT_THREAD_LOCAL      0x01000000UL  /* _Thread_local */
#define FEAT_UNICODE_STR       0x02000000UL  /* u"", U"", u8"" */
#define FEAT_BOOL_KW           0x04000000UL  /* bool/true/false keywords */
#define FEAT_NULLPTR           0x08000000UL  /* nullptr keyword */
#define FEAT_TYPEOF            0x10000000UL  /* typeof/typeof_unqual */
#define FEAT_BIN_LITERAL       0x20000000UL  /* 0b1010 */
#define FEAT_DIGIT_SEP         0x40000000UL  /* 1'000'000 */
#define FEAT_ATTR_SYNTAX       0x80000000UL  /* [[attributes]] */

/* second word of feature flags */
#define FEAT2_CONSTEXPR        0x00000001UL  /* constexpr variables */
#define FEAT2_STATIC_ASSERT_NS 0x00000002UL  /* static_assert no msg */
#define FEAT2_EMPTY_INIT       0x00000004UL  /* int x = {} */
#define FEAT2_LABEL_DECL       0x00000008UL  /* labels before decls */
#define FEAT2_UNNAMED_PARAM    0x00000010UL  /* unnamed fn params */
#define FEAT2_ANON_STRUCT      0x00000020UL  /* anonymous struct/union */
#define FEAT2_NO_IMPLICIT_INT  0x00000040UL  /* require explicit types */
#define FEAT2_COMPLEX          0x00000080UL  /* _Complex type */

/* ---- attribute flags ---- */
/* Keep these in sync with src/cc/ext_attrs.c. */
#define FREE_ATTR_USED           (1U << 2)
#define FREE_ATTR_WEAK           (1U << 3)
#define FREE_ATTR_NOINLINE       (1U << 6)
#define FREE_ATTR_ALWAYS_INLINE  (1U << 7)
#define FREE_ATTR_CONSTRUCTOR    (1U << 14)
#define FREE_ATTR_DESTRUCTOR     (1U << 15)

/* ---- compiler standard state ---- */
struct cc_std {
    int std_level;         /* STD_C89, STD_C99, etc. */
    unsigned long feat;    /* FEAT_* bitmask */
    unsigned long feat2;   /* FEAT2_* bitmask */
};

/* global standard state (defined in cc.c) */
extern struct cc_std cc_std;

/* ---- token types ---- */
enum tok_kind {
    TOK_NUM, TOK_FNUM, TOK_IDENT, TOK_STR, TOK_CHAR_LIT,
    /* operators */
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_AMP, TOK_PIPE, TOK_CARET, TOK_TILDE,
    TOK_LSHIFT, TOK_RSHIFT,
    TOK_AND, TOK_OR, TOK_NOT,
    /* comparison */
    TOK_EQ, TOK_NE, TOK_LT, TOK_GT, TOK_LE, TOK_GE,
    /* assignment */
    TOK_ASSIGN, TOK_PLUS_EQ, TOK_MINUS_EQ, TOK_STAR_EQ, TOK_SLASH_EQ,
    TOK_PERCENT_EQ, TOK_AMP_EQ, TOK_PIPE_EQ, TOK_CARET_EQ,
    TOK_LSHIFT_EQ, TOK_RSHIFT_EQ,
    /* punctuation */
    TOK_SEMI, TOK_COMMA, TOK_DOT, TOK_ARROW, TOK_ELLIPSIS,
    TOK_LPAREN, TOK_RPAREN, TOK_LBRACE, TOK_RBRACE,
    TOK_LBRACKET, TOK_RBRACKET,
    TOK_QUESTION, TOK_COLON, TOK_HASH, TOK_PASTE,
    TOK_INC, TOK_DEC,
    /* C89 keywords */
    TOK_AUTO, TOK_BREAK, TOK_CASE, TOK_CHAR_KW, TOK_CONST,
    TOK_CONTINUE, TOK_DEFAULT, TOK_DO, TOK_DOUBLE, TOK_ELSE,
    TOK_ENUM, TOK_EXTERN, TOK_FLOAT, TOK_FOR, TOK_GOTO,
    TOK_IF, TOK_INT, TOK_LONG, TOK_REGISTER, TOK_RETURN,
    TOK_SHORT, TOK_SIGNED, TOK_SIZEOF, TOK_STATIC, TOK_STRUCT,
    TOK_SWITCH, TOK_TYPEDEF, TOK_UNION, TOK_UNSIGNED, TOK_VOID,
    TOK_VOLATILE, TOK_WHILE,
    /* C99 keywords */
    TOK_BOOL,            /* _Bool */
    TOK_RESTRICT,        /* restrict */
    TOK_INLINE,          /* inline */
    /* C11 keywords */
    TOK_ALIGNAS,         /* _Alignas */
    TOK_ALIGNOF,         /* _Alignof */
    TOK_STATIC_ASSERT,   /* _Static_assert */
    TOK_NORETURN,        /* _Noreturn */
    TOK_GENERIC,         /* _Generic */
    TOK_ATOMIC,          /* _Atomic */
    TOK_THREAD_LOCAL,    /* _Thread_local */
    /* C23 keywords */
    TOK_TRUE,            /* true */
    TOK_FALSE,           /* false */
    TOK_BOOL_KW,         /* bool (C23 keyword, not _Bool) */
    TOK_NULLPTR,         /* nullptr */
    TOK_TYPEOF,          /* typeof */
    TOK_TYPEOF_UNQUAL,   /* typeof_unqual */
    TOK_CONSTEXPR,       /* constexpr */
    TOK_STATIC_ASSERT_KW, /* static_assert (C23 keyword) */
    /* C99 _Complex keyword */
    TOK_COMPLEX,         /* _Complex */
    TOK_ATTR_OPEN,       /* [[ */
    TOK_ATTR_CLOSE,      /* ]] */
    TOK_EOF
};

struct tok {
    enum tok_kind kind;
    long val;
    double fval;
    const char *str;
    const char *raw;
    int len;
    const char *file;
    int line;
    int col;
    int suffix_unsigned; /* 1 if U/u suffix present */
    int suffix_long;     /* 1 if L/l, 2 if LL/ll */
    int suffix_float;    /* 1 if f/F suffix (float literal) */
    int suffix_imaginary;/* 1 if i/j suffix (GNU imaginary literal) */
    int is_hex_or_oct;   /* 1 if hex/octal literal */
    int no_expand;       /* 1 if this ident should not be macro-expanded (blue paint) */
};

/* ---- AST node types ---- */
enum node_kind {
    ND_NUM, ND_FNUM, ND_VAR, ND_STR,
    ND_ADD, ND_SUB, ND_MUL, ND_DIV, ND_MOD,
    ND_BITAND, ND_BITOR, ND_BITXOR, ND_BITNOT,
    ND_SHL, ND_SHR,
    ND_LOGAND, ND_LOGOR, ND_LOGNOT,
    ND_EQ, ND_NE, ND_LT, ND_LE,
    ND_ASSIGN, ND_ADDR, ND_DEREF,
    ND_RETURN, ND_IF, ND_WHILE, ND_FOR, ND_DO,
    ND_BLOCK, ND_CALL, ND_CAST, ND_COMMA_EXPR,
    ND_MEMBER, ND_TERNARY,
    ND_SWITCH, ND_CASE, ND_BREAK, ND_CONTINUE, ND_GOTO, ND_LABEL,
    ND_PRE_INC, ND_PRE_DEC, ND_POST_INC, ND_POST_DEC,
    ND_FUNCDEF,
    /* C99 node kinds */
    ND_VLA_ALLOC,       /* runtime VLA stack allocation */
    ND_COMPOUND_LIT,    /* compound literal */
    ND_DESIG_INIT,      /* designated initializer */
    ND_BOOL_CONV,       /* _Bool conversion (nonzero -> 1) */
    /* C11 node kinds */
    ND_STATIC_ASSERT,   /* _Static_assert */
    ND_GENERIC,         /* _Generic selection */
    /* C23 node kinds */
    ND_NULLPTR,         /* nullptr constant */
    /* initializer list */
    ND_INIT_LIST,       /* {expr, expr, ...} */
    /* variadic function support */
    ND_VA_START,        /* va_start(ap, last_named) */
    ND_VA_ARG,          /* va_arg(ap, type) */
    /* GNU extensions */
    ND_STMT_EXPR,       /* ({ stmt; stmt; expr; }) */
    ND_LABEL_ADDR,      /* &&label (address of label) */
    ND_GOTO_INDIRECT,   /* goto *expr (computed goto) */
    ND_BUILTIN_OVERFLOW, /* __builtin_{add,sub,mul}_overflow */
    ND_GCC_ASM          /* inline asm statement */
};

/* forward declarations */
struct type;
struct member;
struct asm_stmt;

struct node {
    enum node_kind kind;
    struct type *ty;
    struct node *lhs;
    struct node *rhs;
    struct node *next;
    struct node *body;
    struct node *cond;
    struct node *then_;
    struct node *els;
    struct node *init;
    struct node *inc;
    long val;
    double fval;
    char *name;
    struct node *args;
    int offset;
    int label_id;
    int is_vla;          /* 1 if VLA allocation */
    struct node *vla_size; /* runtime size expression for VLA */
    int va_save_offset;  /* stack offset of GP register save area */
    int va_named_gp;     /* count of named GP args (for va_start) */
    int va_named_fp;     /* count of named FP args (for va_start) */
    int va_fp_save_offset; /* stack offset of FP register save area */
    int va_stack_start;  /* byte offset from fp where variadic stack args begin */
    int is_default;      /* 1 if this ND_CASE is a default label */
    long case_end;       /* case range: high value (val=low, case_end=high) */
    int is_case_range;   /* 1 if this is a case range (val...case_end) */
    int bit_width;       /* bitfield width (0 = not a bitfield) */
    int bit_offset;      /* bitfield bit position within storage unit */
    int is_static;       /* 1 if declared with 'static' storage class */
    char *section_name;  /* __attribute__((section("name"))), NULL if unset */
    unsigned int attr_flags; /* attribute flags (ATTR_WEAK, etc.) from ext_attrs */
    struct asm_stmt *asm_data; /* ND_GCC_ASM: parsed inline asm data */
    int is_upvar;    /* 1 if variable is from enclosing function scope */
};

/* ---- type system ---- */
enum type_kind {
    TY_VOID, TY_CHAR, TY_SHORT, TY_INT, TY_LONG,
    TY_FLOAT, TY_DOUBLE, TY_LDOUBLE,
    TY_PTR, TY_ARRAY, TY_STRUCT, TY_UNION, TY_ENUM, TY_FUNC,
    /* C99 types */
    TY_BOOL,         /* _Bool (1 byte, converts nonzero to 1) */
    TY_LLONG,        /* long long (8 bytes) */
    /* C11 types */
    TY_ATOMIC,       /* _Atomic (wrapper, treat as base type) */
    /* C99 complex types */
    TY_COMPLEX_FLOAT,  /* _Complex float (8 bytes: two floats) */
    TY_COMPLEX_DOUBLE, /* _Complex double (16 bytes: two doubles) */
    /* GCC extension */
    TY_INT128          /* __int128 (16 bytes) */
};

struct member {
    struct member *next;
    char *name;
    struct type *ty;
    int offset;
    int bit_width;    /* bitfield width in bits, 0 = not a bitfield */
    int bit_offset;   /* bit position within storage unit */
};

struct type {
    enum type_kind kind;
    int size;
    int align;
    int is_unsigned;
    struct type *base;
    int array_len;
    struct member *members;
    struct type *ret;
    struct type *params;
    struct type *next;
    char *name;
    /* C99 extensions */
    int is_vla;            /* variable-length array */
    struct node *vla_expr; /* runtime element count for VLA */
    int is_restrict;       /* restrict qualifier */
    int is_inline;         /* inline function */
    int is_flex_array;     /* flexible array member */
    int static_array_size; /* static N in array param */
    /* C11 extensions */
    int align_as;          /* _Alignas value, 0 if unset */
    int is_noreturn;       /* _Noreturn function */
    int is_atomic;         /* _Atomic qualifier */
    int is_thread_local;   /* _Thread_local */
    /* C23 extensions */
    int is_constexpr;      /* constexpr variable */
    /* variadic function */
    int is_variadic;       /* function has ... parameter */
    /* C type qualifiers */
    int is_const;          /* const qualifier */
    int is_volatile;       /* volatile qualifier */
    struct type *origin;   /* canonical unqualified type */
};

/* ---- symbol table ---- */
struct symbol {
    struct symbol *next;
    char *name;
    struct type *ty;
    int offset;           /* stack offset for locals */
    int is_local;
    int is_defined;
    struct node *body;    /* function body */
    struct type *params;  /* function params */
    char *asm_label;      /* asm("name") override, NULL if unset */
    char *alias_target;   /* __attribute__((alias("name"))), NULL if unset */
    unsigned int attr_flags; /* attribute flags (weak, etc.) */
};

/* ---- arena allocator ---- */
struct arena {
    char *buf;
    usize cap;
    usize used;
};

void  arena_init(struct arena *a, char *buf, usize cap);
void *arena_alloc(struct arena *a, usize size);
void  arena_reset(struct arena *a);

/* ---- error handling ---- */
void err(const char *fmt, ...);
void err_at(const char *file, int line, int col, const char *fmt, ...);

/* ---- diagnostic / warning infrastructure (diag.c) ---- */

/* Warning categories (bitmask) */
#define W_UNUSED_VAR      0x0001u
#define W_UNUSED_FUNC     0x0002u
#define W_UNUSED_PARAM    0x0004u
#define W_IMPLICIT_FUNC   0x0008u
#define W_RETURN_TYPE     0x0010u
#define W_FORMAT          0x0020u
#define W_SHADOW          0x0040u
#define W_SIGN_COMPARE    0x0080u

extern unsigned int cc_warnings;
extern int cc_werror;
extern int cc_suppress_warnings;
extern int cc_error_count;
extern int cc_warning_count;
extern int cc_error_limit;
extern int cc_in_recovery;

void diag_init(void);
int  diag_had_errors(void);
void diag_warn(const char *file, int line, int col, const char *fmt, ...);
void diag_error(const char *file, int line, int col, const char *fmt, ...);
int  diag_parse_warning_flag(const char *flag);
int  diag_parse_error_limit(const char *flag);
int  diag_warn_enabled(unsigned int category);

/* ---- string utilities ---- */
int  str_eq(const char *a, const char *b);
int  str_eqn(const char *a, const char *b, int n);
char *str_dup(struct arena *a, const char *s, int len);

/* ---- standard level management ---- */
void cc_std_init(int level);
int  cc_has_feat(unsigned long f);
int  cc_has_feat2(unsigned long f);
int  cc_std_at_least(int level);

/* ---- C99 extensions (c99.c) ---- */
void c99_init_keywords(void);
int  c99_is_type_token(struct tok *t);
struct type *c99_parse_type_spec(struct tok *t, struct arena *a);
struct node *c99_parse_for_decl(struct arena *a);
struct node *c99_parse_compound_literal(struct type *ty, struct arena *a);
struct node *c99_parse_designated_init(struct arena *a);
struct type *c99_make_vla(struct type *base, struct node *size_expr,
                          struct arena *a);
void c99_gen_vla_alloc(struct node *n, void *out);
void c99_gen_bool_conv(struct node *n, void *out);
void c99_gen_compound_lit(struct node *n, void *out);

/* ---- C11 extensions (c11.c) ---- */
void c11_init_keywords(void);
int  c11_is_type_token(struct tok *t);
struct node *c11_parse_static_assert(struct arena *a);
struct node *c11_parse_generic(struct arena *a);
void c11_parse_alignas(struct type *ty, struct arena *a);
int  c11_parse_alignof(struct arena *a);
int  c11_generic_match(struct type *controlling, struct type *assoc);
int  type_is_compatible(struct type *a, struct type *b);

/* ---- C23 extensions (c23.c) ---- */
void c23_init_keywords(void);
int  c23_is_type_token(struct tok *t);
struct type *c23_parse_typeof(struct arena *a);
int  c23_parse_attribute(struct arena *a);
int  c23_parse_bin_literal(const char *s, long *val);

#endif /* FREE_H */
