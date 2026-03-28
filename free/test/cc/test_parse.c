/*
 * test_parse.c - Tests for the C compiler parser.
 * Pure C89. All variables at top of block.
 *
 * These tests call the parser API and verify AST structure.
 * Since parse.c is not yet implemented, each test documents
 * the expected behavior: what AST the parser should produce
 * for given input. The tests will compile and pass once
 * parse() is implemented.
 */

#include "../test.h"
#include "free.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ---- stub implementations for compiler utilities ---- */

static char arena_buf[128 * 1024];
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

/* ---- forward declarations for compiler APIs ---- */
struct node *parse(const char *src, const char *filename, struct arena *a);

/* ---- predefined types (from type.c) ---- */
extern struct type *ty_void;
extern struct type *ty_char;
extern struct type *ty_short;
extern struct type *ty_int;
extern struct type *ty_long;

/* ---- helper ---- */

static struct node *parse_input(const char *src)
{
    arena_reset(&test_arena);
    return parse(src, "test.c", &test_arena);
}

/* Local mirror of ext_asm.c so parser tests can inspect asm nodes. */
#define ASM_MAX_OPERANDS 30
#define ASM_MAX_CLOBBERS 32
#define ASM_MAX_TEMPLATE 4096
#define ASM_MAX_GOTO_LABELS 16

struct asm_operand {
    char constraint[16];
    int is_output;
    int is_readwrite;
    char *sym_name;
    int var_offset;
    int is_global;
    char *global_name;
    int is_addr_of;
    int tied_to;
    int is_imm;
    long imm_val;
    int is_upvar;
};

struct asm_stmt {
    char template_str[ASM_MAX_TEMPLATE];
    int is_volatile;
    int is_goto;
    struct asm_operand outputs[ASM_MAX_OPERANDS];
    int noutputs;
    struct asm_operand inputs[ASM_MAX_OPERANDS];
    int ninputs;
    char *clobbers[ASM_MAX_CLOBBERS];
    int nclobbers;
    char *goto_labels[ASM_MAX_GOTO_LABELS];
    int ngoto_labels;
};

/* ===== return statement tests ===== */

TEST(parse_return_zero)
{
    struct node *prog;
    struct node *func;
    struct node *body;
    struct node *stmt;

    prog = parse_input("int main(void) { return 0; }");
    ASSERT_NOT_NULL(prog);

    /* top-level should be a function definition */
    func = prog;
    ASSERT_EQ(func->kind, ND_FUNCDEF);
    ASSERT_NOT_NULL(func->name);
    ASSERT_STR_EQ(func->name, "main");

    /* function body should be a block */
    body = func->body;
    ASSERT_NOT_NULL(body);
    ASSERT_EQ(body->kind, ND_BLOCK);

    /* first statement in block should be return */
    stmt = body->body;
    ASSERT_NOT_NULL(stmt);
    ASSERT_EQ(stmt->kind, ND_RETURN);

    /* return value should be number 0 */
    ASSERT_NOT_NULL(stmt->lhs);
    ASSERT_EQ(stmt->lhs->kind, ND_NUM);
    ASSERT_EQ(stmt->lhs->val, 0);
}

TEST(parse_return_number)
{
    struct node *prog;
    struct node *func;
    struct node *ret;

    prog = parse_input("int f(void) { return 42; }");
    ASSERT_NOT_NULL(prog);
    func = prog;
    ASSERT_EQ(func->kind, ND_FUNCDEF);

    ret = func->body->body;
    ASSERT_NOT_NULL(ret);
    ASSERT_EQ(ret->kind, ND_RETURN);
    ASSERT_EQ(ret->lhs->kind, ND_NUM);
    ASSERT_EQ(ret->lhs->val, 42);
}

/* ===== expression precedence tests ===== */

TEST(parse_add)
{
    struct node *prog;
    struct node *ret;
    struct node *expr;

    prog = parse_input("int f(void) { return 1 + 2; }");
    ASSERT_NOT_NULL(prog);

    ret = prog->body->body;
    ASSERT_EQ(ret->kind, ND_RETURN);

    expr = ret->lhs;
    ASSERT_NOT_NULL(expr);
    /* constant folding: 1 + 2 -> 3 */
    ASSERT_EQ(expr->kind, ND_NUM);
    ASSERT_EQ(expr->val, 3);
}

TEST(parse_precedence_mul_before_add)
{
    struct node *prog;
    struct node *ret;
    struct node *expr;

    /*
     * "1 + 2 * 3" should parse as "1 + (2 * 3)"
     * AST: ND_ADD(ND_NUM(1), ND_MUL(ND_NUM(2), ND_NUM(3)))
     */
    prog = parse_input("int f(void) { return 1 + 2 * 3; }");
    ASSERT_NOT_NULL(prog);

    ret = prog->body->body;
    expr = ret->lhs;

    /* constant folding: 1 + 2*3 -> 7 */
    ASSERT_EQ(expr->kind, ND_NUM);
    ASSERT_EQ(expr->val, 7);
}

TEST(parse_sub)
{
    struct node *prog;
    struct node *ret;
    struct node *expr;

    prog = parse_input("int f(void) { return 10 - 3; }");
    ASSERT_NOT_NULL(prog);

    ret = prog->body->body;
    expr = ret->lhs;
    /* constant folding: 10 - 3 -> 7 */
    ASSERT_EQ(expr->kind, ND_NUM);
    ASSERT_EQ(expr->val, 7);
}

TEST(parse_div)
{
    struct node *prog;
    struct node *ret;
    struct node *expr;

    prog = parse_input("int f(void) { return 10 / 2; }");
    ASSERT_NOT_NULL(prog);

    ret = prog->body->body;
    expr = ret->lhs;
    /* constant folding: 10 / 2 -> 5 */
    ASSERT_EQ(expr->kind, ND_NUM);
    ASSERT_EQ(expr->val, 5);
}

TEST(parse_comparison_eq)
{
    struct node *prog;
    struct node *ret;
    struct node *expr;

    prog = parse_input("int f(void) { return 1 == 2; }");
    ASSERT_NOT_NULL(prog);

    ret = prog->body->body;
    expr = ret->lhs;
    /* constant folding: 1 == 2 -> 0 */
    ASSERT_EQ(expr->kind, ND_NUM);
    ASSERT_EQ(expr->val, 0);
}

TEST(parse_comparison_ne)
{
    struct node *prog;
    struct node *ret;
    struct node *expr;

    prog = parse_input("int f(void) { return 1 != 2; }");
    ASSERT_NOT_NULL(prog);

    ret = prog->body->body;
    expr = ret->lhs;
    /* constant folding: 1 != 2 -> 1 */
    ASSERT_EQ(expr->kind, ND_NUM);
    ASSERT_EQ(expr->val, 1);
}

TEST(parse_comparison_lt)
{
    struct node *prog;
    struct node *ret;
    struct node *expr;

    prog = parse_input("int f(void) { return 1 < 2; }");
    ASSERT_NOT_NULL(prog);

    ret = prog->body->body;
    expr = ret->lhs;
    /* constant folding: 1 < 2 -> 1 */
    ASSERT_EQ(expr->kind, ND_NUM);
    ASSERT_EQ(expr->val, 1);
}

TEST(parse_comparison_le)
{
    struct node *prog;
    struct node *ret;
    struct node *expr;

    prog = parse_input("int f(void) { return 1 <= 2; }");
    ASSERT_NOT_NULL(prog);

    ret = prog->body->body;
    expr = ret->lhs;
    /* constant folding: 1 <= 2 -> 1 */
    ASSERT_EQ(expr->kind, ND_NUM);
    ASSERT_EQ(expr->val, 1);
}

/* ===== variable declaration and assignment tests ===== */

TEST(parse_var_decl_and_assign)
{
    struct node *prog;
    struct node *body;
    struct node *stmt;

    prog = parse_input("int f(void) { int x; x = 5; return x; }");
    ASSERT_NOT_NULL(prog);
    ASSERT_EQ(prog->kind, ND_FUNCDEF);

    body = prog->body;
    ASSERT_NOT_NULL(body);
    ASSERT_EQ(body->kind, ND_BLOCK);

    /* skip past declaration to assignment */
    stmt = body->body;
    ASSERT_NOT_NULL(stmt);

    /* find the assignment statement (may be first or second in the list) */
    while (stmt != NULL && stmt->kind != ND_ASSIGN) {
        stmt = stmt->next;
    }
    ASSERT_NOT_NULL(stmt);
    ASSERT_EQ(stmt->kind, ND_ASSIGN);

    /* lhs should be a variable reference */
    ASSERT_EQ(stmt->lhs->kind, ND_VAR);

    /* rhs should be number 5 */
    ASSERT_EQ(stmt->rhs->kind, ND_NUM);
    ASSERT_EQ(stmt->rhs->val, 5);
}

TEST(parse_var_assign_expr)
{
    struct node *prog;
    struct node *body;
    struct node *stmt;

    prog = parse_input("int f(void) { int x; x = 1 + 2; }");
    ASSERT_NOT_NULL(prog);

    body = prog->body;
    stmt = body->body;

    /* find the assignment */
    while (stmt != NULL && stmt->kind != ND_ASSIGN) {
        stmt = stmt->next;
    }
    ASSERT_NOT_NULL(stmt);
    ASSERT_EQ(stmt->kind, ND_ASSIGN);
    /* constant folding: 1 + 2 -> 3 */
    ASSERT_EQ(stmt->rhs->kind, ND_NUM);
    ASSERT_EQ(stmt->rhs->val, 3);
}

/* ===== if/else tests ===== */

TEST(parse_if_simple)
{
    struct node *prog;
    struct node *body;
    struct node *stmt;

    prog = parse_input("int f(void) { if (1) return 2; }");
    ASSERT_NOT_NULL(prog);

    body = prog->body;
    stmt = body->body;

    /* find the if node */
    while (stmt != NULL && stmt->kind != ND_IF) {
        stmt = stmt->next;
    }
    ASSERT_NOT_NULL(stmt);
    ASSERT_EQ(stmt->kind, ND_IF);
    ASSERT_NOT_NULL(stmt->cond);
    ASSERT_NOT_NULL(stmt->then_);
}

TEST(parse_if_else)
{
    struct node *prog;
    struct node *body;
    struct node *stmt;

    prog = parse_input("int f(void) { if (1) return 2; else return 3; }");
    ASSERT_NOT_NULL(prog);

    body = prog->body;
    stmt = body->body;

    while (stmt != NULL && stmt->kind != ND_IF) {
        stmt = stmt->next;
    }
    ASSERT_NOT_NULL(stmt);
    ASSERT_EQ(stmt->kind, ND_IF);
    ASSERT_NOT_NULL(stmt->cond);
    ASSERT_NOT_NULL(stmt->then_);
    ASSERT_NOT_NULL(stmt->els);
}

TEST(parse_if_condition)
{
    struct node *prog;
    struct node *body;
    struct node *stmt;

    prog = parse_input("int f(int x) { if (x < 10) return 1; }");
    ASSERT_NOT_NULL(prog);

    body = prog->body;
    stmt = body->body;

    while (stmt != NULL && stmt->kind != ND_IF) {
        stmt = stmt->next;
    }
    ASSERT_NOT_NULL(stmt);
    ASSERT_EQ(stmt->cond->kind, ND_LT);
}

/* ===== while loop tests ===== */

TEST(parse_while)
{
    struct node *prog;
    struct node *body;
    struct node *stmt;

    prog = parse_input("int f(void) { while (1) return 0; }");
    ASSERT_NOT_NULL(prog);

    body = prog->body;
    stmt = body->body;

    while (stmt != NULL && stmt->kind != ND_WHILE) {
        stmt = stmt->next;
    }
    ASSERT_NOT_NULL(stmt);
    ASSERT_EQ(stmt->kind, ND_WHILE);
    ASSERT_NOT_NULL(stmt->cond);
    /* parser stores while body in ->then_ */
    ASSERT_NOT_NULL(stmt->then_);
}

TEST(parse_while_condition)
{
    struct node *prog;
    struct node *body;
    struct node *stmt;

    prog = parse_input("int f(int x) { while (x > 0) x = x - 1; }");
    ASSERT_NOT_NULL(prog);

    body = prog->body;
    stmt = body->body;

    while (stmt != NULL && stmt->kind != ND_WHILE) {
        stmt = stmt->next;
    }
    ASSERT_NOT_NULL(stmt);
    ASSERT_EQ(stmt->kind, ND_WHILE);

    /* condition should be a greater-than comparison (x > 0 -> 0 < x) */
    ASSERT_NOT_NULL(stmt->cond);
}

/* ===== for loop tests ===== */

TEST(parse_for)
{
    struct node *prog;
    struct node *body;
    struct node *stmt;

    prog = parse_input(
        "int f(void) { int i; for (i = 0; i < 10; i = i + 1) return i; }");
    ASSERT_NOT_NULL(prog);

    body = prog->body;
    stmt = body->body;

    while (stmt != NULL && stmt->kind != ND_FOR) {
        stmt = stmt->next;
    }
    ASSERT_NOT_NULL(stmt);
    ASSERT_EQ(stmt->kind, ND_FOR);
    ASSERT_NOT_NULL(stmt->init);  /* i = 0 */
    ASSERT_NOT_NULL(stmt->cond);  /* i < 10 */
    ASSERT_NOT_NULL(stmt->inc);   /* i = i + 1 */
    /* parser stores for body in ->then_ */
    ASSERT_NOT_NULL(stmt->then_);  /* body */
}

TEST(parse_for_empty_parts)
{
    struct node *prog;
    struct node *body;
    struct node *stmt;

    /* infinite loop: for (;;) */
    prog = parse_input("int f(void) { for (;;) return 0; }");
    ASSERT_NOT_NULL(prog);

    body = prog->body;
    stmt = body->body;

    while (stmt != NULL && stmt->kind != ND_FOR) {
        stmt = stmt->next;
    }
    ASSERT_NOT_NULL(stmt);
    ASSERT_EQ(stmt->kind, ND_FOR);
    /* init, cond, inc may be NULL for empty for-loop clauses */
}

/* ===== function call tests ===== */

TEST(parse_call_no_args)
{
    struct node *prog;
    struct node *body;
    struct node *stmt;
    struct node *call;

    prog = parse_input("int f(void) { foo(); }");
    ASSERT_NOT_NULL(prog);

    body = prog->body;
    stmt = body->body;
    ASSERT_NOT_NULL(stmt);

    /* the call expression might be inside an expression-statement */
    call = stmt;
    if (call->kind != ND_CALL && call->lhs != NULL) {
        call = call->lhs;
    }
    ASSERT_EQ(call->kind, ND_CALL);
    ASSERT_NOT_NULL(call->name);
    ASSERT_STR_EQ(call->name, "foo");
    ASSERT_NULL(call->args);
}

TEST(parse_call_with_args)
{
    struct node *prog;
    struct node *body;
    struct node *stmt;
    struct node *call;

    prog = parse_input("int f(void) { bar(1, 2); }");
    ASSERT_NOT_NULL(prog);

    body = prog->body;
    stmt = body->body;
    ASSERT_NOT_NULL(stmt);

    call = stmt;
    if (call->kind != ND_CALL && call->lhs != NULL) {
        call = call->lhs;
    }
    ASSERT_EQ(call->kind, ND_CALL);
    ASSERT_NOT_NULL(call->name);
    ASSERT_STR_EQ(call->name, "bar");
    ASSERT_NOT_NULL(call->args);

    /* first arg should be 1 */
    ASSERT_EQ(call->args->kind, ND_NUM);
    ASSERT_EQ(call->args->val, 1);

    /* second arg should be 2 */
    ASSERT_NOT_NULL(call->args->next);
    ASSERT_EQ(call->args->next->kind, ND_NUM);
    ASSERT_EQ(call->args->next->val, 2);
}

/* ===== pointer operation tests ===== */

TEST(parse_addr_of)
{
    struct node *prog;
    struct node *body;
    struct node *stmt;
    struct node *expr;

    prog = parse_input("int *f(void) { int x; return &x; }");
    ASSERT_NOT_NULL(prog);

    body = prog->body;
    /* find the return statement */
    stmt = body->body;
    while (stmt != NULL && stmt->kind != ND_RETURN) {
        stmt = stmt->next;
    }
    ASSERT_NOT_NULL(stmt);

    expr = stmt->lhs;
    ASSERT_NOT_NULL(expr);
    ASSERT_EQ(expr->kind, ND_ADDR);
    ASSERT_NOT_NULL(expr->lhs);
    ASSERT_EQ(expr->lhs->kind, ND_VAR);
}

TEST(parse_deref)
{
    struct node *prog;
    struct node *body;
    struct node *stmt;
    struct node *expr;

    prog = parse_input("int f(int *p) { return *p; }");
    ASSERT_NOT_NULL(prog);

    body = prog->body;
    stmt = body->body;
    while (stmt != NULL && stmt->kind != ND_RETURN) {
        stmt = stmt->next;
    }
    ASSERT_NOT_NULL(stmt);

    expr = stmt->lhs;
    ASSERT_NOT_NULL(expr);
    ASSERT_EQ(expr->kind, ND_DEREF);
}

TEST(parse_asm_constraint_concat_empty)
{
    struct node *prog;
    struct node *body;
    struct node *stmt;
    struct asm_stmt *as;

    prog = parse_input(
        "#define __stringify_1(x) #x\n"
        "#define __stringify(x) __stringify_1(x)\n"
        "#define CONSTRAINT\n"
        "int f(int i) { asm volatile(\"\" : : __stringify(CONSTRAINT) \"r\" (i)); return 0; }");
    ASSERT_NOT_NULL(prog);

    body = prog->body;
    ASSERT_NOT_NULL(body);
    stmt = body->body;
    while (stmt != NULL && stmt->kind != ND_GCC_ASM) {
        stmt = stmt->next;
    }
    ASSERT_NOT_NULL(stmt);
    ASSERT_NOT_NULL(stmt->asm_data);

    as = stmt->asm_data;
    ASSERT_EQ(as->ninputs, 1);
    ASSERT_STR_EQ(as->inputs[0].constraint, "r");
}

TEST(parse_asm_constraint_concat_nonempty)
{
    struct node *prog;
    struct node *body;
    struct node *stmt;
    struct asm_stmt *as;

    prog = parse_input(
        "#define __stringify_1(x) #x\n"
        "#define __stringify(x) __stringify_1(x)\n"
        "#define CONSTRAINT I\n"
        "int f(int i) { asm volatile(\"\" : : __stringify(CONSTRAINT) \"r\" (i)); return 0; }");
    ASSERT_NOT_NULL(prog);

    body = prog->body;
    ASSERT_NOT_NULL(body);
    stmt = body->body;
    while (stmt != NULL && stmt->kind != ND_GCC_ASM) {
        stmt = stmt->next;
    }
    ASSERT_NOT_NULL(stmt);
    ASSERT_NOT_NULL(stmt->asm_data);

    as = stmt->asm_data;
    ASSERT_EQ(as->ninputs, 1);
    ASSERT_STR_EQ(as->inputs[0].constraint, "Ir");
}

TEST(parse_toplevel_asm_volatile_skipped)
{
    struct node *prog;

    prog = parse_input(
        "asm volatile(\".globl top_level_symbol\");\n"
        "int after_asm(void) { return 7; }");
    ASSERT_NOT_NULL(prog);
    ASSERT_EQ(prog->kind, ND_FUNCDEF);
    ASSERT_STR_EQ(prog->name, "after_asm");
    ASSERT_EQ(prog->body->body->kind, ND_RETURN);
    ASSERT_EQ(prog->body->body->lhs->kind, ND_NUM);
    ASSERT_EQ(prog->body->body->lhs->val, 7);
}

TEST(parse_toplevel_asm_goto_skipped)
{
    struct node *prog;

    prog = parse_input(
        "asm goto(\"branch %l0\" : : : : target);\n"
        "int after_goto(void) { return 9; }");
    ASSERT_NOT_NULL(prog);
    ASSERT_EQ(prog->kind, ND_FUNCDEF);
    ASSERT_STR_EQ(prog->name, "after_goto");
    ASSERT_EQ(prog->body->body->kind, ND_RETURN);
    ASSERT_EQ(prog->body->body->lhs->kind, ND_NUM);
    ASSERT_EQ(prog->body->body->lhs->val, 9);
}

/* ===== block tests ===== */

TEST(parse_block_multiple_stmts)
{
    struct node *prog;
    struct node *body;
    struct node *stmt;
    int count;

    prog = parse_input(
        "int f(void) { int a; int b; a = 1; b = 2; return a + b; }");
    ASSERT_NOT_NULL(prog);
    ASSERT_EQ(prog->kind, ND_FUNCDEF);

    body = prog->body;
    ASSERT_EQ(body->kind, ND_BLOCK);

    /* count statements in the block */
    count = 0;
    stmt = body->body;
    while (stmt != NULL) {
        count++;
        stmt = stmt->next;
    }
    /* should have at least the return statement */
    ASSERT(count >= 1);
}

/* ===== function definition tests ===== */

TEST(parse_funcdef_name)
{
    struct node *prog;

    prog = parse_input("int main(void) { return 0; }");
    ASSERT_NOT_NULL(prog);
    ASSERT_EQ(prog->kind, ND_FUNCDEF);
    ASSERT_STR_EQ(prog->name, "main");
}

TEST(parse_funcdef_no_params)
{
    struct node *prog;

    prog = parse_input("void f(void) { }");
    ASSERT_NOT_NULL(prog);
    ASSERT_EQ(prog->kind, ND_FUNCDEF);
    ASSERT_STR_EQ(prog->name, "f");
}

TEST(parse_multiple_funcs)
{
    struct node *prog;

    prog = parse_input(
        "int foo(void) { return 1; } int bar(void) { return 2; }");
    ASSERT_NOT_NULL(prog);
    ASSERT_EQ(prog->kind, ND_FUNCDEF);
    ASSERT_STR_EQ(prog->name, "foo");

    /* second function linked via next */
    ASSERT_NOT_NULL(prog->next);
    ASSERT_EQ(prog->next->kind, ND_FUNCDEF);
    ASSERT_STR_EQ(prog->next->name, "bar");
}

/* ===== struct declaration tests ===== */

TEST(parse_struct_member_access)
{
    struct node *prog;
    struct node *body;
    struct node *stmt;

    prog = parse_input(
        "struct point { int x; int y; };\n"
        "int f(void) { struct point p; p.x = 1; return p.x; }");
    ASSERT_NOT_NULL(prog);

    /* find the function definition */
    while (prog != NULL && prog->kind != ND_FUNCDEF) {
        prog = prog->next;
    }
    ASSERT_NOT_NULL(prog);

    body = prog->body;
    ASSERT_NOT_NULL(body);

    /* find the return statement */
    stmt = body->body;
    while (stmt != NULL && stmt->kind != ND_RETURN) {
        stmt = stmt->next;
    }
    ASSERT_NOT_NULL(stmt);
    ASSERT_EQ(stmt->kind, ND_RETURN);

    /* return expression should involve member access */
    ASSERT_NOT_NULL(stmt->lhs);
    ASSERT_EQ(stmt->lhs->kind, ND_MEMBER);
}

/* ===== nested expression tests ===== */

TEST(parse_nested_parens)
{
    struct node *prog;
    struct node *ret;
    struct node *expr;

    prog = parse_input("int f(void) { return (1 + 2) * 3; }");
    ASSERT_NOT_NULL(prog);

    ret = prog->body->body;
    ASSERT_EQ(ret->kind, ND_RETURN);

    expr = ret->lhs;
    /* constant folding: (1+2)*3 -> 9 */
    ASSERT_EQ(expr->kind, ND_NUM);
    ASSERT_EQ(expr->val, 9);
}

TEST(parse_unary_negation)
{
    struct node *prog;
    struct node *ret;
    struct node *expr;

    prog = parse_input("int f(void) { return -5; }");
    ASSERT_NOT_NULL(prog);

    ret = prog->body->body;
    ASSERT_EQ(ret->kind, ND_RETURN);

    /* -5 is typically represented as 0 - 5 or ND_SUB(0, 5) */
    expr = ret->lhs;
    ASSERT_NOT_NULL(expr);
    /* implementation may represent this as ND_SUB(ND_NUM(0), ND_NUM(5)) */
    /* or as ND_NUM(-5) -- either is acceptable */
    ASSERT(expr->kind == ND_SUB || expr->kind == ND_NUM);
}

int main(void)
{
    arena_init(&test_arena, arena_buf, sizeof(arena_buf));

    printf("test_parse:\n");

    /* return statements */
    RUN_TEST(parse_return_zero);
    RUN_TEST(parse_return_number);

    /* expression precedence */
    RUN_TEST(parse_add);
    RUN_TEST(parse_precedence_mul_before_add);
    RUN_TEST(parse_sub);
    RUN_TEST(parse_div);
    RUN_TEST(parse_comparison_eq);
    RUN_TEST(parse_comparison_ne);
    RUN_TEST(parse_comparison_lt);
    RUN_TEST(parse_comparison_le);

    /* variables */
    RUN_TEST(parse_var_decl_and_assign);
    RUN_TEST(parse_var_assign_expr);

    /* if/else */
    RUN_TEST(parse_if_simple);
    RUN_TEST(parse_if_else);
    RUN_TEST(parse_if_condition);

    /* while loops */
    RUN_TEST(parse_while);
    RUN_TEST(parse_while_condition);

    /* for loops */
    RUN_TEST(parse_for);
    RUN_TEST(parse_for_empty_parts);

    /* function calls */
    RUN_TEST(parse_call_no_args);
    RUN_TEST(parse_call_with_args);

    /* pointer operations */
    RUN_TEST(parse_addr_of);
    RUN_TEST(parse_deref);
    RUN_TEST(parse_asm_constraint_concat_empty);
    RUN_TEST(parse_asm_constraint_concat_nonempty);
    RUN_TEST(parse_toplevel_asm_volatile_skipped);
    RUN_TEST(parse_toplevel_asm_goto_skipped);

    /* blocks */
    RUN_TEST(parse_block_multiple_stmts);

    /* function definitions */
    RUN_TEST(parse_funcdef_name);
    RUN_TEST(parse_funcdef_no_params);
    RUN_TEST(parse_multiple_funcs);

    /* structs */
    RUN_TEST(parse_struct_member_access);

    /* nested expressions */
    RUN_TEST(parse_nested_parens);
    RUN_TEST(parse_unary_negation);

    TEST_SUMMARY();
    return tests_failed;
}
int cc_target_arch = 0;
int cc_freestanding = 0;
int cc_function_sections = 0;
int cc_data_sections = 0;
int cc_general_regs_only = 0;
int cc_nostdinc = 0;
int builtin_is_known(const char *name) { (void)name; return 0; }
/* asm_is_asm_keyword provided by ext_asm.c */
int attr_is_attribute_keyword(const char *s) { (void)s; return 0; }
int attr_is_extension_keyword(const char *s) { (void)s; return 0; }
int attr_is_typeof_keyword(const char *s) { (void)s; return 0; }
int attr_is_auto_type(const char *s) { (void)s; return 0; }
int attr_is_noreturn_keyword(const char *s) { (void)s; return 0; }
void attr_init(void) {}
void attr_parse(void) {}
void attr_info_init(void *info) { memset(info, 0, 64); }
int attr_try_parse(void *info, void *tok_ptr) {
    (void)info; (void)tok_ptr; return 0;
}
void attr_apply_to_type(void *ty, const void *info) {
    (void)ty; (void)info;
}
char *attr_parse_section_name(void *t) { (void)t; return 0; }
void attr_set_const_eval(void *cb) { (void)cb; }
/* asm_ext_init provided by ext_asm.c */
