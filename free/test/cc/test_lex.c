/*
 * test_lex.c - Tests for the C compiler lexer.
 * Pure C89. All variables at top of block.
 */

#include "../test.h"
#include "free.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ---- stub implementations for compiler utilities ---- */

static char arena_buf[64 * 1024];
static struct arena test_arena;

void arena_init(struct arena *a, char *buf, usize cap)
{
    a->buf = buf;
    a->cap = cap;
    a->used = 0;
}

void *arena_alloc(struct arena *a, usize size)
{
    void *p;

    size = (size + 7) & ~(usize)7;
    if (a->used + size > a->cap) {
        fprintf(stderr, "arena_alloc: out of memory\n");
        exit(1);
    }
    p = a->buf + a->used;
    a->used += size;
    return p;
}

void arena_reset(struct arena *a)
{
    a->used = 0;
}

int str_eq(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

int str_eqn(const char *a, const char *b, int n)
{
    return strncmp(a, b, n) == 0;
}

char *str_dup(struct arena *a, const char *s, int len)
{
    char *p;

    p = (char *)arena_alloc(a, len + 1);
    memcpy(p, s, len);
    p[len] = '\0';
    return p;
}

void err(const char *fmt, ...)
{
    va_list ap;
    (void)fmt;
    va_start(ap, fmt);
    va_end(ap);
    fprintf(stderr, "error\n");
    exit(1);
}

void err_at(const char *file, int line, int col, const char *fmt, ...)
{
    va_list ap;
    (void)file;
    (void)line;
    (void)col;
    (void)fmt;
    va_start(ap, fmt);
    va_end(ap);
    fprintf(stderr, "error at %s:%d:%d\n", file, line, col);
    exit(1);
}

/* ---- stubs for cc_std functions (defined in cc.c) ---- */
struct cc_std cc_std;

int cc_has_feat(unsigned long f)
{
    return (cc_std.feat & f) != 0;
}

int cc_has_feat2(unsigned long f)
{
    return (cc_std.feat2 & f) != 0;
}

/* ---- lexer declarations ---- */
void lex_init(const char *src, const char *filename, struct arena *a);
struct tok *lex_next(void);
struct tok *lex_peek(void);
int lex_get_line(void);
int lex_get_col(void);

/* ---- helper: reset arena and re-init lexer ---- */

static void lex_setup(const char *src)
{
    arena_reset(&test_arena);
    lex_init(src, "test.c", &test_arena);
}

/* ===== number tests ===== */

TEST(lex_decimal)
{
    struct tok *t;

    lex_setup("42");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 42);
}

TEST(lex_zero)
{
    struct tok *t;

    lex_setup("0");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 0);
}

TEST(lex_hex_lower)
{
    struct tok *t;

    lex_setup("0xff");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 255);
}

TEST(lex_hex_upper)
{
    struct tok *t;

    lex_setup("0XFF");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 255);
}

TEST(lex_hex_mixed)
{
    struct tok *t;

    lex_setup("0xDeAdBeEf");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, (long)0xDeAdBeEf);
}

TEST(lex_octal)
{
    struct tok *t;

    lex_setup("0777");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 511);
}

TEST(lex_octal_zero)
{
    struct tok *t;

    lex_setup("00");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 0);
}

TEST(lex_decimal_large)
{
    struct tok *t;

    lex_setup("123456789");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 123456789);
}

/* ===== integer suffix tests ===== */

TEST(lex_suffix_ul)
{
    struct tok *t;

    lex_setup("0UL");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 0);
    ASSERT_EQ(t->suffix_unsigned, 1);
    ASSERT_EQ(t->suffix_long, 1);
}

TEST(lex_suffix_u)
{
    struct tok *t;

    lex_setup("42U");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 42);
    ASSERT_EQ(t->suffix_unsigned, 1);
    ASSERT_EQ(t->suffix_long, 0);
}

TEST(lex_suffix_l)
{
    struct tok *t;

    lex_setup("100L");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 100);
    ASSERT_EQ(t->suffix_unsigned, 0);
    ASSERT_EQ(t->suffix_long, 1);
}

TEST(lex_suffix_ull)
{
    struct tok *t;

    lex_setup("1ULL");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 1);
    ASSERT_EQ(t->suffix_unsigned, 1);
    ASSERT_EQ(t->suffix_long, 2);
}

TEST(lex_suffix_none)
{
    struct tok *t;

    lex_setup("99");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 99);
    ASSERT_EQ(t->suffix_unsigned, 0);
    ASSERT_EQ(t->suffix_long, 0);
}

TEST(lex_suffix_lu)
{
    struct tok *t;

    lex_setup("5LU");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 5);
    ASSERT_EQ(t->suffix_unsigned, 1);
    ASSERT_EQ(t->suffix_long, 1);
}

/* ===== identifier tests ===== */

TEST(lex_ident_simple)
{
    struct tok *t;

    lex_setup("foo");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_NOT_NULL(t->str);
    ASSERT_STR_EQ(t->str, "foo");
}

TEST(lex_ident_underscore)
{
    struct tok *t;

    lex_setup("_bar");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "_bar");
}

TEST(lex_ident_with_digits)
{
    struct tok *t;

    lex_setup("x123");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "x123");
}

TEST(lex_ident_all_underscores)
{
    struct tok *t;

    lex_setup("__foo__");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "__foo__");
}

TEST(lex_ident_single_char)
{
    struct tok *t;

    lex_setup("x");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "x");
}

/* ===== keyword tests ===== */

TEST(lex_kw_int)
{
    struct tok *t;

    lex_setup("int");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_INT);
}

TEST(lex_kw_return)
{
    struct tok *t;

    lex_setup("return");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_RETURN);
}

TEST(lex_kw_if)
{
    struct tok *t;

    lex_setup("if");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IF);
}

TEST(lex_kw_else)
{
    struct tok *t;

    lex_setup("else");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_ELSE);
}

TEST(lex_kw_while)
{
    struct tok *t;

    lex_setup("while");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_WHILE);
}

TEST(lex_kw_for)
{
    struct tok *t;

    lex_setup("for");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_FOR);
}

TEST(lex_kw_void)
{
    struct tok *t;

    lex_setup("void");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_VOID);
}

TEST(lex_kw_struct)
{
    struct tok *t;

    lex_setup("struct");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_STRUCT);
}

TEST(lex_kw_char)
{
    struct tok *t;

    lex_setup("char");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_CHAR_KW);
}

TEST(lex_kw_sizeof)
{
    struct tok *t;

    lex_setup("sizeof");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_SIZEOF);
}

TEST(lex_kw_typedef)
{
    struct tok *t;

    lex_setup("typedef");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_TYPEDEF);
}

TEST(lex_kw_not_prefix)
{
    struct tok *t;

    /* "integer" is not the keyword "int" */
    lex_setup("integer");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "integer");
}

/* ===== operator tests ===== */

TEST(lex_op_plus)
{
    struct tok *t;

    lex_setup("+");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_PLUS);
}

TEST(lex_op_plus_eq)
{
    struct tok *t;

    lex_setup("+=");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_PLUS_EQ);
}

TEST(lex_op_increment)
{
    struct tok *t;

    lex_setup("++");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_INC);
}

TEST(lex_op_minus)
{
    struct tok *t;

    lex_setup("-");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_MINUS);
}

TEST(lex_op_minus_eq)
{
    struct tok *t;

    lex_setup("-=");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_MINUS_EQ);
}

TEST(lex_op_decrement)
{
    struct tok *t;

    lex_setup("--");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_DEC);
}

TEST(lex_op_arrow)
{
    struct tok *t;

    lex_setup("->");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_ARROW);
}

TEST(lex_op_star)
{
    struct tok *t;

    lex_setup("*");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_STAR);
}

TEST(lex_op_star_eq)
{
    struct tok *t;

    lex_setup("*=");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_STAR_EQ);
}

TEST(lex_op_slash)
{
    struct tok *t;

    lex_setup("/ 1");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_SLASH);
}

TEST(lex_op_eq)
{
    struct tok *t;

    lex_setup("==");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_EQ);
}

TEST(lex_op_ne)
{
    struct tok *t;

    lex_setup("!=");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_NE);
}

TEST(lex_op_assign)
{
    struct tok *t;

    lex_setup("=");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_ASSIGN);
}

TEST(lex_op_lt)
{
    struct tok *t;

    lex_setup("<");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_LT);
}

TEST(lex_op_le)
{
    struct tok *t;

    lex_setup("<=");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_LE);
}

TEST(lex_op_gt)
{
    struct tok *t;

    lex_setup(">");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_GT);
}

TEST(lex_op_ge)
{
    struct tok *t;

    lex_setup(">=");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_GE);
}

TEST(lex_op_lshift)
{
    struct tok *t;

    lex_setup("<<");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_LSHIFT);
}

TEST(lex_op_rshift)
{
    struct tok *t;

    lex_setup(">>");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_RSHIFT);
}

TEST(lex_op_and)
{
    struct tok *t;

    lex_setup("&&");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_AND);
}

TEST(lex_op_or)
{
    struct tok *t;

    lex_setup("||");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_OR);
}

TEST(lex_op_not)
{
    struct tok *t;

    lex_setup("!");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_NOT);
}

TEST(lex_op_amp)
{
    struct tok *t;

    lex_setup("&");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_AMP);
}

TEST(lex_op_pipe)
{
    struct tok *t;

    lex_setup("|");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_PIPE);
}

TEST(lex_op_caret)
{
    struct tok *t;

    lex_setup("^");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_CARET);
}

TEST(lex_op_tilde)
{
    struct tok *t;

    lex_setup("~");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_TILDE);
}

TEST(lex_op_ellipsis)
{
    struct tok *t;

    lex_setup("...");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_ELLIPSIS);
}

/* ===== punctuation tests ===== */

TEST(lex_punct_semi)
{
    struct tok *t;

    lex_setup(";");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_SEMI);
}

TEST(lex_punct_comma)
{
    struct tok *t;

    lex_setup(",");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_COMMA);
}

TEST(lex_punct_parens)
{
    struct tok *t;

    lex_setup("()");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_LPAREN);
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_RPAREN);
}

TEST(lex_punct_braces)
{
    struct tok *t;

    lex_setup("{}");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_LBRACE);
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_RBRACE);
}

TEST(lex_punct_brackets)
{
    struct tok *t;

    lex_setup("[]");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_LBRACKET);
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_RBRACKET);
}

TEST(lex_punct_question_colon)
{
    struct tok *t;

    lex_setup("?:");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_QUESTION);
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_COLON);
}

TEST(lex_punct_hash)
{
    struct tok *t;

    lex_setup("#");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_HASH);
}

TEST(lex_punct_dot)
{
    struct tok *t;

    lex_setup(".");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_DOT);
}

/* ===== string literal tests ===== */

TEST(lex_string_simple)
{
    struct tok *t;

    lex_setup("\"hello\"");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_STR);
    ASSERT_NOT_NULL(t->str);
    ASSERT_STR_EQ(t->str, "hello");
    ASSERT_EQ(t->len, 5);
}

TEST(lex_string_empty)
{
    struct tok *t;

    lex_setup("\"\"");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_STR);
    ASSERT_EQ(t->len, 0);
}

TEST(lex_string_escape_newline)
{
    struct tok *t;

    lex_setup("\"a\\nb\"");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_STR);
    ASSERT_EQ(t->len, 3);
    ASSERT_EQ(t->str[0], 'a');
    ASSERT_EQ(t->str[1], '\n');
    ASSERT_EQ(t->str[2], 'b');
}

TEST(lex_string_escape_tab)
{
    struct tok *t;

    lex_setup("\"\\t\"");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_STR);
    ASSERT_EQ(t->len, 1);
    ASSERT_EQ(t->str[0], '\t');
}

TEST(lex_string_escape_backslash)
{
    struct tok *t;

    lex_setup("\"\\\\\"");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_STR);
    ASSERT_EQ(t->len, 1);
    ASSERT_EQ(t->str[0], '\\');
}

TEST(lex_string_escape_quote)
{
    struct tok *t;

    lex_setup("\"\\\"\"");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_STR);
    ASSERT_EQ(t->len, 1);
    ASSERT_EQ(t->str[0], '"');
}

TEST(lex_string_escape_hex)
{
    struct tok *t;

    lex_setup("\"\\x41\"");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_STR);
    ASSERT_EQ(t->len, 1);
    ASSERT_EQ(t->str[0], 'A');
}

TEST(lex_string_escape_octal)
{
    struct tok *t;

    lex_setup("\"\\101\"");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_STR);
    ASSERT_EQ(t->len, 1);
    ASSERT_EQ(t->str[0], 'A');
}

/* ===== char literal tests ===== */

TEST(lex_char_simple)
{
    struct tok *t;

    lex_setup("'a'");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_CHAR_LIT);
    ASSERT_EQ(t->val, 97);
}

TEST(lex_char_zero)
{
    struct tok *t;

    lex_setup("'0'");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_CHAR_LIT);
    ASSERT_EQ(t->val, '0');
}

TEST(lex_char_escape_n)
{
    struct tok *t;

    lex_setup("'\\n'");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_CHAR_LIT);
    ASSERT_EQ(t->val, 10);
}

TEST(lex_char_escape_t)
{
    struct tok *t;

    lex_setup("'\\t'");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_CHAR_LIT);
    ASSERT_EQ(t->val, 9);
}

TEST(lex_char_escape_zero)
{
    struct tok *t;

    lex_setup("'\\0'");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_CHAR_LIT);
    ASSERT_EQ(t->val, 0);
}

TEST(lex_char_escape_backslash)
{
    struct tok *t;

    lex_setup("'\\\\'");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_CHAR_LIT);
    ASSERT_EQ(t->val, '\\');
}

TEST(lex_char_escape_quote)
{
    struct tok *t;

    lex_setup("'\\''");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_CHAR_LIT);
    ASSERT_EQ(t->val, '\'');
}

/* ===== multi-token sequence tests ===== */

TEST(lex_sequence_decl)
{
    struct tok *t;

    lex_setup("int x = 42;");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_INT);

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "x");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_ASSIGN);

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 42);

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_SEMI);

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_EOF);
}

TEST(lex_sequence_expr)
{
    struct tok *t;

    lex_setup("a + b * c");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "a");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_PLUS);

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "b");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_STAR);

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "c");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_EOF);
}

TEST(lex_sequence_func_sig)
{
    struct tok *t;

    lex_setup("int main(void)");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_INT);

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "main");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_LPAREN);

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_VOID);

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_RPAREN);

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_EOF);
}

TEST(lex_sequence_ptr_deref)
{
    struct tok *t;

    lex_setup("*p = &x;");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_STAR);

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "p");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_ASSIGN);

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_AMP);

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "x");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_SEMI);
}

/* ===== comment tests ===== */

TEST(lex_skip_block_comment)
{
    struct tok *t;

    lex_setup("x /* this is a comment */ y");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "x");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "y");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_EOF);
}

TEST(lex_skip_line_comment)
{
    struct tok *t;

    lex_setup("x // line comment\ny");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "x");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "y");
}

TEST(lex_skip_multiline_comment)
{
    struct tok *t;

    lex_setup("a /* multi\nline\ncomment */ b");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "a");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "b");
}

TEST(lex_skip_adjacent_comments)
{
    struct tok *t;

    lex_setup("a /* c1 */ /* c2 */ b");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "a");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "b");
}

/* ===== line/column tracking tests ===== */

TEST(lex_position_first_token)
{
    struct tok *t;

    lex_setup("hello");
    t = lex_next();
    ASSERT_EQ(t->line, 1);
    ASSERT_EQ(t->col, 1);
}

TEST(lex_position_after_spaces)
{
    struct tok *t;

    lex_setup("   x");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_EQ(t->line, 1);
    ASSERT_EQ(t->col, 4);
}

TEST(lex_position_second_line)
{
    struct tok *t;

    lex_setup("a\nb");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_EQ(t->line, 1);

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_EQ(t->line, 2);
}

TEST(lex_position_multi_line)
{
    struct tok *t;

    lex_setup("a\n\n\nb");

    t = lex_next();
    ASSERT_EQ(t->line, 1);

    t = lex_next();
    ASSERT_EQ(t->line, 4);
}

TEST(lex_filename_tracked)
{
    struct tok *t;

    lex_setup("x");
    t = lex_next();
    ASSERT_NOT_NULL(t->file);
    ASSERT_STR_EQ(t->file, "test.c");
}

/* ===== EOF tests ===== */

TEST(lex_eof_empty)
{
    struct tok *t;

    lex_setup("");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_EOF);
}

TEST(lex_eof_whitespace_only)
{
    struct tok *t;

    lex_setup("   \n\t  \n  ");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_EOF);
}

TEST(lex_eof_after_token)
{
    struct tok *t;

    lex_setup("42");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_EOF);
}

TEST(lex_eof_repeated)
{
    struct tok *t;

    lex_setup("");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_EOF);
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_EOF);
}

/* ===== peek tests ===== */

TEST(lex_peek_does_not_consume)
{
    struct tok *t;

    lex_setup("42");
    t = lex_peek();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 42);

    /* peek again returns same token */
    t = lex_peek();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 42);

    /* next consumes it */
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 42);

    /* now we get EOF */
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_EOF);
}

TEST(lex_peek_then_next)
{
    struct tok *t;

    lex_setup("a b");

    t = lex_peek();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "a");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "a");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "b");
}

/* ===== whitespace handling tests ===== */

TEST(lex_tabs_and_spaces)
{
    struct tok *t;

    lex_setup("a\t\tb");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "a");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "b");
}

TEST(lex_no_space_between_ops)
{
    struct tok *t;

    lex_setup("a+b");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_PLUS);

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
}

/* ===== missing operator tests ===== */

TEST(lex_op_percent)
{
    struct tok *t;

    lex_setup("%");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_PERCENT);
}

TEST(lex_op_slash_eq)
{
    struct tok *t;

    lex_setup("/=");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_SLASH_EQ);
}

TEST(lex_op_percent_eq)
{
    struct tok *t;

    lex_setup("%=");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_PERCENT_EQ);
}

TEST(lex_op_amp_eq)
{
    struct tok *t;

    lex_setup("&=");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_AMP_EQ);
}

TEST(lex_op_pipe_eq)
{
    struct tok *t;

    lex_setup("|=");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_PIPE_EQ);
}

TEST(lex_op_caret_eq)
{
    struct tok *t;

    lex_setup("^=");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_CARET_EQ);
}

TEST(lex_op_lshift_eq)
{
    struct tok *t;

    lex_setup("<<=");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_LSHIFT_EQ);
}

TEST(lex_op_rshift_eq)
{
    struct tok *t;

    lex_setup(">>=");
    t = lex_next();
    ASSERT_EQ(t->kind, TOK_RSHIFT_EQ);
}

/* ===== full program tokenization test ===== */

TEST(lex_sequence_full_program)
{
    struct tok *t;

    lex_setup("int main(void) { return 42; }");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_INT);

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_IDENT);
    ASSERT_STR_EQ(t->str, "main");

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_LPAREN);

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_VOID);

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_RPAREN);

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_LBRACE);

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_RETURN);

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_NUM);
    ASSERT_EQ(t->val, 42);

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_SEMI);

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_RBRACE);

    t = lex_next();
    ASSERT_EQ(t->kind, TOK_EOF);
}

int main(void)
{
    arena_init(&test_arena, arena_buf, sizeof(arena_buf));

    printf("test_lex:\n");

    /* numbers */
    RUN_TEST(lex_decimal);
    RUN_TEST(lex_zero);
    RUN_TEST(lex_hex_lower);
    RUN_TEST(lex_hex_upper);
    RUN_TEST(lex_hex_mixed);
    RUN_TEST(lex_octal);
    RUN_TEST(lex_octal_zero);
    RUN_TEST(lex_decimal_large);

    /* integer suffixes */
    RUN_TEST(lex_suffix_ul);
    RUN_TEST(lex_suffix_u);
    RUN_TEST(lex_suffix_l);
    RUN_TEST(lex_suffix_ull);
    RUN_TEST(lex_suffix_none);
    RUN_TEST(lex_suffix_lu);

    /* identifiers */
    RUN_TEST(lex_ident_simple);
    RUN_TEST(lex_ident_underscore);
    RUN_TEST(lex_ident_with_digits);
    RUN_TEST(lex_ident_all_underscores);
    RUN_TEST(lex_ident_single_char);

    /* keywords */
    RUN_TEST(lex_kw_int);
    RUN_TEST(lex_kw_return);
    RUN_TEST(lex_kw_if);
    RUN_TEST(lex_kw_else);
    RUN_TEST(lex_kw_while);
    RUN_TEST(lex_kw_for);
    RUN_TEST(lex_kw_void);
    RUN_TEST(lex_kw_struct);
    RUN_TEST(lex_kw_char);
    RUN_TEST(lex_kw_sizeof);
    RUN_TEST(lex_kw_typedef);
    RUN_TEST(lex_kw_not_prefix);

    /* operators */
    RUN_TEST(lex_op_plus);
    RUN_TEST(lex_op_plus_eq);
    RUN_TEST(lex_op_increment);
    RUN_TEST(lex_op_minus);
    RUN_TEST(lex_op_minus_eq);
    RUN_TEST(lex_op_decrement);
    RUN_TEST(lex_op_arrow);
    RUN_TEST(lex_op_star);
    RUN_TEST(lex_op_star_eq);
    RUN_TEST(lex_op_slash);
    RUN_TEST(lex_op_eq);
    RUN_TEST(lex_op_ne);
    RUN_TEST(lex_op_assign);
    RUN_TEST(lex_op_lt);
    RUN_TEST(lex_op_le);
    RUN_TEST(lex_op_gt);
    RUN_TEST(lex_op_ge);
    RUN_TEST(lex_op_lshift);
    RUN_TEST(lex_op_rshift);
    RUN_TEST(lex_op_and);
    RUN_TEST(lex_op_or);
    RUN_TEST(lex_op_not);
    RUN_TEST(lex_op_amp);
    RUN_TEST(lex_op_pipe);
    RUN_TEST(lex_op_caret);
    RUN_TEST(lex_op_tilde);
    RUN_TEST(lex_op_ellipsis);
    RUN_TEST(lex_op_percent);
    RUN_TEST(lex_op_slash_eq);
    RUN_TEST(lex_op_percent_eq);
    RUN_TEST(lex_op_amp_eq);
    RUN_TEST(lex_op_pipe_eq);
    RUN_TEST(lex_op_caret_eq);
    RUN_TEST(lex_op_lshift_eq);
    RUN_TEST(lex_op_rshift_eq);

    /* punctuation */
    RUN_TEST(lex_punct_semi);
    RUN_TEST(lex_punct_comma);
    RUN_TEST(lex_punct_parens);
    RUN_TEST(lex_punct_braces);
    RUN_TEST(lex_punct_brackets);
    RUN_TEST(lex_punct_question_colon);
    RUN_TEST(lex_punct_hash);
    RUN_TEST(lex_punct_dot);

    /* string literals */
    RUN_TEST(lex_string_simple);
    RUN_TEST(lex_string_empty);
    RUN_TEST(lex_string_escape_newline);
    RUN_TEST(lex_string_escape_tab);
    RUN_TEST(lex_string_escape_backslash);
    RUN_TEST(lex_string_escape_quote);
    RUN_TEST(lex_string_escape_hex);
    RUN_TEST(lex_string_escape_octal);

    /* char literals */
    RUN_TEST(lex_char_simple);
    RUN_TEST(lex_char_zero);
    RUN_TEST(lex_char_escape_n);
    RUN_TEST(lex_char_escape_t);
    RUN_TEST(lex_char_escape_zero);
    RUN_TEST(lex_char_escape_backslash);
    RUN_TEST(lex_char_escape_quote);

    /* multi-token sequences */
    RUN_TEST(lex_sequence_decl);
    RUN_TEST(lex_sequence_expr);
    RUN_TEST(lex_sequence_func_sig);
    RUN_TEST(lex_sequence_ptr_deref);

    /* comments */
    RUN_TEST(lex_skip_block_comment);
    RUN_TEST(lex_skip_line_comment);
    RUN_TEST(lex_skip_multiline_comment);
    RUN_TEST(lex_skip_adjacent_comments);

    /* line/column tracking */
    RUN_TEST(lex_position_first_token);
    RUN_TEST(lex_position_after_spaces);
    RUN_TEST(lex_position_second_line);
    RUN_TEST(lex_position_multi_line);
    RUN_TEST(lex_filename_tracked);

    /* EOF */
    RUN_TEST(lex_eof_empty);
    RUN_TEST(lex_eof_whitespace_only);
    RUN_TEST(lex_eof_after_token);
    RUN_TEST(lex_eof_repeated);

    /* peek */
    RUN_TEST(lex_peek_does_not_consume);
    RUN_TEST(lex_peek_then_next);

    /* whitespace */
    RUN_TEST(lex_tabs_and_spaces);
    RUN_TEST(lex_no_space_between_ops);

    /* full program tokenization */
    RUN_TEST(lex_sequence_full_program);

    TEST_SUMMARY();
    return tests_failed;
}
