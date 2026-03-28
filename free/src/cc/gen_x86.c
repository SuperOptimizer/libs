/*
 * gen_x86.c - x86_64 code generator for the free C compiler.
 * Emits GAS-compatible AT&T syntax assembly from an AST.
 * Stack-based expression evaluation: result always in %rax.
 * System V AMD64 ABI: args in rdi, rsi, rdx, rcx, r8, r9.
 * Pure C89. No external dependencies.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "free.h"

extern int cc_function_sections;
extern int cc_data_sections;

/* ---- internal state ---- */
static FILE *out;
static int label_count;
static int depth;            /* stack depth tracking (in 8-byte slots) */
static char *current_func;   /* name of function being compiled */
static int current_break_label;    /* target for break statements */
static int current_continue_label; /* target for continue statements */

/* ---- forward declarations ---- */
static void gen_x86_expr(struct node *n);
static void gen_x86_stmt(struct node *n);
static void gen_x86_addr(struct node *n);

/* ---- output helpers ---- */

static void emit(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(out, "\t");
    vfprintf(out, fmt, ap);
    fprintf(out, "\n");
    va_end(ap);
}

static void emit_label(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    fprintf(out, ":\n");
    va_end(ap);
}

static void emit_comment(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(out, "\t/* ");
    vfprintf(out, fmt, ap);
    fprintf(out, " */\n");
    va_end(ap);
}

static int new_label(void)
{
    return label_count++;
}

/* ---- stack management ---- */

static void push(void)
{
    emit("pushq %%rax");
    depth++;
}

static void pop(const char *reg)
{
    emit("popq %s", reg);
    depth--;
}

/* ---- type helpers ---- */

static int align_to(int val, int a)
{
    return (val + a - 1) & ~(a - 1);
}

/* ---- load/store by type size ---- */

/*
 * emit_load - load value from address in %rax, result in %rax.
 */
static void emit_load(struct type *ty)
{
    int sz;

    if (ty == NULL) {
        emit("movq (%%rax), %%rax");
        return;
    }

    sz = ty->size;

    if (ty->kind == TY_ARRAY || ty->kind == TY_STRUCT ||
        ty->kind == TY_UNION) {
        return;
    }

    if (sz == 1) {
        if (ty->is_unsigned) {
            emit("movzbl (%%rax), %%eax");
        } else {
            emit("movsbl (%%rax), %%eax");
        }
    } else if (sz == 2) {
        if (ty->is_unsigned) {
            emit("movzwl (%%rax), %%eax");
        } else {
            emit("movswl (%%rax), %%eax");
        }
    } else if (sz == 4) {
        if (ty->is_unsigned) {
            emit("movl (%%rax), %%eax");
        } else {
            emit("movslq (%%rax), %%rax");
        }
    } else {
        emit("movq (%%rax), %%rax");
    }
}

/*
 * emit_store - store value from %rax to address in %rcx.
 */
static void emit_store(struct type *ty)
{
    int sz;
    int i;

    if (ty == NULL) {
        emit("movq %%rax, (%%rcx)");
        return;
    }

    sz = ty->size;

    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        for (i = 0; i + 8 <= sz; i += 8) {
            emit("movq %d(%%rax), %%rdx", i);
            emit("movq %%rdx, %d(%%rcx)", i);
        }
        for (; i + 4 <= sz; i += 4) {
            emit("movl %d(%%rax), %%edx", i);
            emit("movl %%edx, %d(%%rcx)", i);
        }
        for (; i < sz; i++) {
            emit("movzbl %d(%%rax), %%edx", i);
            emit("movb %%dl, %d(%%rcx)", i);
        }
        return;
    }

    if (sz == 1) {
        emit("movb %%al, (%%rcx)");
    } else if (sz == 2) {
        emit("movw %%ax, (%%rcx)");
    } else if (sz == 4) {
        emit("movl %%eax, (%%rcx)");
    } else {
        emit("movq %%rax, (%%rcx)");
    }
}

/* ---- constant loading ---- */

static void emit_num(long val)
{
    if (val == 0) {
        emit("xorl %%eax, %%eax");
        return;
    }

    if (val > 0 && val <= 0x7FFFFFFFL) {
        emit("movl $%ld, %%eax", val);
        return;
    }

    emit("movabsq $%ld, %%rax", val);
}

/* ---- address generation ---- */

static void gen_x86_addr(struct node *n)
{
    switch (n->kind) {
    case ND_VAR:
        if (n->offset != 0) {
            emit_comment("addr of local '%s' [rbp-%d]",
                         n->name ? n->name : "?", n->offset);
            emit("leaq -%d(%%rbp), %%rax", n->offset);
        } else {
            emit_comment("addr of global '%s'",
                         n->name ? n->name : "?");
            emit("leaq %s(%%rip), %%rax", n->name);
        }
        return;

    case ND_DEREF:
        gen_x86_expr(n->lhs);
        return;

    case ND_MEMBER:
        gen_x86_addr(n->lhs);
        if (n->offset != 0) {
            emit("addq $%d, %%rax", n->offset);
        }
        return;

    default:
        fprintf(stderr, "gen_x86_addr: not an lvalue (kind=%d)\n",
                n->kind);
        exit(1);
    }
}

static int inc_dec_step(struct node *operand)
{
    if (operand->ty != NULL &&
        operand->ty->kind == TY_PTR &&
        operand->ty->base != NULL) {
        return operand->ty->base->size;
    }
    return 1;
}

/* ---- expression generation ---- */

/* System V AMD64 argument registers */
static const char *arg_regs64[6] = {
    "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"
};

static void gen_x86_expr(struct node *n)
{
    struct node *arg;
    int nargs;
    int lbl;
    int sz;
    int i;

    if (n == NULL) {
        return;
    }

    switch (n->kind) {
    case ND_NUM:
        emit_num(n->val);
        return;

    case ND_VAR:
        gen_x86_addr(n);
        emit_load(n->ty);
        return;

    case ND_STR:
        emit("leaq .LS%d(%%rip), %%rax", n->label_id);
        return;

    case ND_ASSIGN:
        gen_x86_addr(n->lhs);
        push();
        gen_x86_expr(n->rhs);
        pop("%rcx");
        emit_store(n->lhs->ty);
        return;

    case ND_ADDR:
        gen_x86_addr(n->lhs);
        return;

    case ND_DEREF:
        gen_x86_expr(n->lhs);
        emit_load(n->ty);
        return;

    case ND_MEMBER:
        gen_x86_addr(n);
        emit_load(n->ty);
        return;

    case ND_LOGNOT:
        gen_x86_expr(n->lhs);
        emit("cmpq $0, %%rax");
        emit("sete %%al");
        emit("movzbl %%al, %%eax");
        return;

    case ND_BITNOT:
        gen_x86_expr(n->lhs);
        emit("notq %%rax");
        return;

    case ND_LOGAND:
        lbl = new_label();
        gen_x86_expr(n->lhs);
        emit("cmpq $0, %%rax");
        emit("je .L.false.%d", lbl);
        gen_x86_expr(n->rhs);
        emit("cmpq $0, %%rax");
        emit("je .L.false.%d", lbl);
        emit("movl $1, %%eax");
        emit("jmp .L.end.%d", lbl);
        emit_label(".L.false.%d", lbl);
        emit("xorl %%eax, %%eax");
        emit_label(".L.end.%d", lbl);
        return;

    case ND_LOGOR:
        lbl = new_label();
        gen_x86_expr(n->lhs);
        emit("cmpq $0, %%rax");
        emit("jne .L.true.%d", lbl);
        gen_x86_expr(n->rhs);
        emit("cmpq $0, %%rax");
        emit("jne .L.true.%d", lbl);
        emit("xorl %%eax, %%eax");
        emit("jmp .L.end.%d", lbl);
        emit_label(".L.true.%d", lbl);
        emit("movl $1, %%eax");
        emit_label(".L.end.%d", lbl);
        return;

    case ND_CALL:
        nargs = 0;
        for (arg = n->args; arg; arg = arg->next) {
            nargs++;
        }

        /* evaluate args, push each to stack */
        for (arg = n->args; arg; arg = arg->next) {
            gen_x86_expr(arg);
            push();
        }

        /* pop args into registers in reverse order */
        for (i = nargs - 1; i >= 0; i--) {
            pop(arg_regs64[i]);
        }

        /*
         * Align stack to 16 bytes before call.
         * The ABI requires 16-byte alignment at the call instruction.
         * We push %rax if depth is odd (making it even).
         */
        if (depth % 2 != 0) {
            emit("subq $8, %%rsp");
        }

        /* zero %al for variadic function calls */
        emit("xorl %%eax, %%eax");
        emit("call %s", n->name);

        if (depth % 2 != 0) {
            emit("addq $8, %%rsp");
        }
        return;

    case ND_COMMA_EXPR:
        gen_x86_expr(n->lhs);
        gen_x86_expr(n->rhs);
        return;

    case ND_CAST:
        gen_x86_expr(n->lhs);
        if (n->ty != NULL) {
            sz = n->ty->size;
            if (sz == 1) {
                if (n->ty->is_unsigned) {
                    emit("movzbl %%al, %%eax");
                } else {
                    emit("movsbl %%al, %%eax");
                }
            } else if (sz == 2) {
                if (n->ty->is_unsigned) {
                    emit("movzwl %%ax, %%eax");
                } else {
                    emit("movswl %%ax, %%eax");
                }
            } else if (sz == 4) {
                if (n->ty->is_unsigned) {
                    emit("movl %%eax, %%eax");
                } else {
                    emit("cltq");
                }
            }
        }
        return;

    case ND_TERNARY:
        lbl = new_label();
        gen_x86_expr(n->cond);
        emit("cmpq $0, %%rax");
        emit("je .L.else.%d", lbl);
        gen_x86_expr(n->then_);
        emit("jmp .L.end.%d", lbl);
        emit_label(".L.else.%d", lbl);
        gen_x86_expr(n->els);
        emit_label(".L.end.%d", lbl);
        return;

    case ND_PRE_INC:
        sz = inc_dec_step(n->lhs);
        gen_x86_addr(n->lhs);
        push();
        emit("movq %%rax, %%rcx");
        emit_load(n->lhs->ty);
        emit("addq $%d, %%rax", sz);
        pop("%rcx");
        emit_store(n->lhs->ty);
        return;

    case ND_PRE_DEC:
        sz = inc_dec_step(n->lhs);
        gen_x86_addr(n->lhs);
        push();
        emit("movq %%rax, %%rcx");
        emit_load(n->lhs->ty);
        emit("subq $%d, %%rax", sz);
        pop("%rcx");
        emit_store(n->lhs->ty);
        return;

    case ND_POST_INC:
        sz = inc_dec_step(n->lhs);
        gen_x86_addr(n->lhs);
        push();                         /* push address */
        emit("movq %%rax, %%rcx");
        emit_load(n->lhs->ty);
        push();                         /* push original value */
        emit("addq $%d, %%rax", sz);
        emit("movq 8(%%rsp), %%rcx");   /* reload address */
        emit_store(n->lhs->ty);
        pop("%rax");                    /* restore original value */
        emit("addq $8, %%rsp");         /* discard saved address */
        depth--;
        return;

    case ND_POST_DEC:
        sz = inc_dec_step(n->lhs);
        gen_x86_addr(n->lhs);
        push();                         /* push address */
        emit("movq %%rax, %%rcx");
        emit_load(n->lhs->ty);
        push();                         /* push original value */
        emit("subq $%d, %%rax", sz);
        emit("movq 8(%%rsp), %%rcx");   /* reload address */
        emit_store(n->lhs->ty);
        pop("%rax");                    /* restore original value */
        emit("addq $8, %%rsp");         /* discard saved address */
        depth--;
        return;

    default:
        break;
    }

    /* binary operations: eval rhs, push, eval lhs, pop rhs to %rcx */
    gen_x86_expr(n->rhs);
    push();
    gen_x86_expr(n->lhs);
    pop("%rcx");

    /* now %rax = lhs, %rcx = rhs */
    switch (n->kind) {
    case ND_ADD:
        if (n->ty != NULL && n->ty->kind == TY_PTR &&
            n->ty->base != NULL) {
            sz = n->ty->base->size;
            if (sz > 1) {
                emit("imulq $%d, %%rcx", sz);
            }
        }
        emit("addq %%rcx, %%rax");
        return;

    case ND_SUB:
        if (n->lhs != NULL && n->lhs->ty != NULL &&
            n->lhs->ty->kind == TY_PTR &&
            n->lhs->ty->base != NULL &&
            n->rhs != NULL && n->rhs->ty != NULL &&
            n->rhs->ty->kind == TY_PTR) {
            emit("subq %%rcx, %%rax");
            sz = n->lhs->ty->base->size;
            if (sz > 1) {
                emit("cqto");
                emit("movq $%d, %%rcx", sz);
                emit("idivq %%rcx");
            }
        } else if (n->ty != NULL && n->ty->kind == TY_PTR &&
                   n->ty->base != NULL) {
            sz = n->ty->base->size;
            if (sz > 1) {
                emit("imulq $%d, %%rcx", sz);
            }
            emit("subq %%rcx, %%rax");
        } else {
            emit("subq %%rcx, %%rax");
        }
        return;

    case ND_MUL:
        emit("imulq %%rcx, %%rax");
        return;

    case ND_DIV:
        if (n->ty != NULL && n->ty->is_unsigned) {
            emit("xorl %%edx, %%edx");
            emit("divq %%rcx");
        } else {
            emit("cqto");
            emit("idivq %%rcx");
        }
        return;

    case ND_MOD:
        if (n->ty != NULL && n->ty->is_unsigned) {
            emit("xorl %%edx, %%edx");
            emit("divq %%rcx");
        } else {
            emit("cqto");
            emit("idivq %%rcx");
        }
        emit("movq %%rdx, %%rax");
        return;

    case ND_BITAND:
        emit("andq %%rcx, %%rax");
        return;

    case ND_BITOR:
        emit("orq %%rcx, %%rax");
        return;

    case ND_BITXOR:
        emit("xorq %%rcx, %%rax");
        return;

    case ND_SHL:
        /* shift count must be in %cl */
        emit("shlq %%cl, %%rax");
        return;

    case ND_SHR:
        if (n->ty != NULL && n->ty->is_unsigned) {
            emit("shrq %%cl, %%rax");
        } else {
            emit("sarq %%cl, %%rax");
        }
        return;

    case ND_EQ:
        emit("cmpq %%rcx, %%rax");
        emit("sete %%al");
        emit("movzbl %%al, %%eax");
        return;

    case ND_NE:
        emit("cmpq %%rcx, %%rax");
        emit("setne %%al");
        emit("movzbl %%al, %%eax");
        return;

    case ND_LT:
        emit("cmpq %%rcx, %%rax");
        if (n->lhs != NULL && n->lhs->ty != NULL &&
            n->lhs->ty->is_unsigned) {
            emit("setb %%al");
        } else {
            emit("setl %%al");
        }
        emit("movzbl %%al, %%eax");
        return;

    case ND_LE:
        emit("cmpq %%rcx, %%rax");
        if (n->lhs != NULL && n->lhs->ty != NULL &&
            n->lhs->ty->is_unsigned) {
            emit("setbe %%al");
        } else {
            emit("setle %%al");
        }
        emit("movzbl %%al, %%eax");
        return;

    default:
        fprintf(stderr, "gen_x86_expr: unknown node kind %d\n",
                n->kind);
        exit(1);
    }
}

/* ---- statement generation ---- */

static void gen_x86_stmt(struct node *n)
{
    int lbl;
    int saved_break;
    int saved_continue;
    struct node *cur;

    if (n == NULL) {
        return;
    }

    switch (n->kind) {
    case ND_RETURN:
        if (n->lhs != NULL) {
            gen_x86_expr(n->lhs);
        }
        emit("jmp .L.return.%s", current_func);
        return;

    case ND_BLOCK:
        for (cur = n->body; cur != NULL; cur = cur->next) {
            gen_x86_stmt(cur);
        }
        return;

    case ND_IF:
        lbl = new_label();
        gen_x86_expr(n->cond);
        emit("cmpq $0, %%rax");
        if (n->els != NULL) {
            emit("je .L.else.%d", lbl);
            gen_x86_stmt(n->then_);
            emit("jmp .L.end.%d", lbl);
            emit_label(".L.else.%d", lbl);
            gen_x86_stmt(n->els);
            emit_label(".L.end.%d", lbl);
        } else {
            emit("je .L.end.%d", lbl);
            gen_x86_stmt(n->then_);
            emit_label(".L.end.%d", lbl);
        }
        return;

    case ND_WHILE:
        lbl = new_label();
        saved_break = current_break_label;
        saved_continue = current_continue_label;
        current_break_label = lbl;
        current_continue_label = lbl;

        emit_label(".L.begin.%d", lbl);
        gen_x86_expr(n->cond);
        emit("cmpq $0, %%rax");
        emit("je .L.break.%d", lbl);
        gen_x86_stmt(n->then_);
        emit_label(".L.continue.%d", lbl);
        emit("jmp .L.begin.%d", lbl);
        emit_label(".L.break.%d", lbl);

        current_break_label = saved_break;
        current_continue_label = saved_continue;
        return;

    case ND_FOR:
        lbl = new_label();
        saved_break = current_break_label;
        saved_continue = current_continue_label;
        current_break_label = lbl;
        current_continue_label = lbl;

        if (n->init != NULL) {
            gen_x86_stmt(n->init);
        }
        emit_label(".L.begin.%d", lbl);
        if (n->cond != NULL) {
            gen_x86_expr(n->cond);
            emit("cmpq $0, %%rax");
            emit("je .L.break.%d", lbl);
        }
        gen_x86_stmt(n->then_);
        emit_label(".L.continue.%d", lbl);
        if (n->inc != NULL) {
            gen_x86_expr(n->inc);
        }
        emit("jmp .L.begin.%d", lbl);
        emit_label(".L.break.%d", lbl);

        current_break_label = saved_break;
        current_continue_label = saved_continue;
        return;

    case ND_DO:
        lbl = new_label();
        saved_break = current_break_label;
        saved_continue = current_continue_label;
        current_break_label = lbl;
        current_continue_label = lbl;

        emit_label(".L.begin.%d", lbl);
        gen_x86_stmt(n->then_);
        emit_label(".L.continue.%d", lbl);
        gen_x86_expr(n->cond);
        emit("cmpq $0, %%rax");
        emit("jne .L.begin.%d", lbl);
        emit_label(".L.break.%d", lbl);

        current_break_label = saved_break;
        current_continue_label = saved_continue;
        return;

    case ND_SWITCH:
        lbl = new_label();
        saved_break = current_break_label;
        current_break_label = lbl;

        gen_x86_expr(n->cond);
        emit("movq %%rax, %%r10"); /* save switch value */

        for (cur = n->body; cur != NULL; cur = cur->next) {
            if (cur->kind == ND_CASE) {
                cur->label_id = new_label();
                emit("cmpq $%ld, %%r10", cur->val);
                emit("je .L.case.%d", cur->label_id);
            }
        }

        emit("jmp .L.break.%d", lbl);

        for (cur = n->body; cur != NULL; cur = cur->next) {
            gen_x86_stmt(cur);
        }

        emit_label(".L.break.%d", lbl);
        current_break_label = saved_break;
        return;

    case ND_CASE:
        emit_label(".L.case.%d", n->label_id);
        gen_x86_stmt(n->lhs);
        return;

    case ND_BREAK:
        if (current_break_label < 0) {
            fprintf(stderr, "gen_x86: break outside loop/switch\n");
            exit(1);
        }
        emit("jmp .L.break.%d", current_break_label);
        return;

    case ND_CONTINUE:
        if (current_continue_label < 0) {
            fprintf(stderr, "gen_x86: continue outside loop\n");
            exit(1);
        }
        emit("jmp .L.continue.%d", current_continue_label);
        return;

    case ND_GOTO:
        emit("jmp .L.label.%s.%s", current_func, n->name);
        return;

    case ND_LABEL:
        emit_label(".L.label.%s.%s", current_func, n->name);
        gen_x86_stmt(n->lhs);
        return;

    default:
        gen_x86_expr(n);
        return;
    }
}

/* ---- string literal collection ---- */

/*
 * emit_string_directive - emit a .string with proper escaping.
 */
static void emit_string_directive(const char *s)
{
    const unsigned char *p;

    fprintf(out, "\t.string \"");
    if (s != NULL) {
        for (p = (const unsigned char *)s; *p != '\0'; p++) {
            switch (*p) {
            case '\n': fprintf(out, "\\n");  break;
            case '\r': fprintf(out, "\\r");  break;
            case '\t': fprintf(out, "\\t");  break;
            case '\0': fprintf(out, "\\0");  break;
            case '\\': fprintf(out, "\\\\"); break;
            case '"':  fprintf(out, "\\\""); break;
            default:
                if (*p < 0x20 || *p >= 0x7f) {
                    fprintf(out, "\\%03o", *p);
                } else {
                    fputc(*p, out);
                }
                break;
            }
        }
    }
    fprintf(out, "\"\n");
}

static void collect_strings_node(struct node *n)
{
    struct node *cur;

    if (n == NULL) {
        return;
    }

    if (n->kind == ND_STR) {
        fprintf(out, ".LS%d:\n", n->label_id);
        emit_string_directive(n->name);
    }

    collect_strings_node(n->lhs);
    collect_strings_node(n->rhs);
    /* walk the body chain (compound statement lists) */
    for (cur = n->body; cur != NULL; cur = cur->next) {
        collect_strings_node(cur);
    }
    collect_strings_node(n->cond);
    collect_strings_node(n->then_);
    collect_strings_node(n->els);
    collect_strings_node(n->init);
    collect_strings_node(n->inc);
    /* walk the args chain (function call arguments) */
    for (cur = n->args; cur != NULL; cur = cur->next) {
        collect_strings_node(cur);
    }
}

static void collect_all_strings(struct node *prog)
{
    struct node *n;

    for (n = prog; n != NULL; n = n->next) {
        collect_strings_node(n);
    }
}

/* ---- function definition ---- */

static void gen_x86_funcdef(struct node *n)
{
    int stack_size;
    struct node *param;
    int i;
    static const char *param_regs64[6] = {
        "rdi", "rsi", "rdx", "rcx", "r8", "r9"
    };

    current_func = n->name;
    current_break_label = -1;
    current_continue_label = -1;
    depth = 0;

    fprintf(out, "\n");
    if (cc_function_sections) {
        fprintf(out, "\t.section .text.%s,\"ax\",@progbits\n",
                n->name);
    }
    if (n->is_static) {
        fprintf(out, "\t.local %s\n", n->name);
    } else {
        fprintf(out, "\t.globl %s\n", n->name);
    }
    fprintf(out, "\t.type %s, @function\n", n->name);
    fprintf(out, "\t.p2align 4\n");
    emit_label("%s", n->name);

    /*
     * Frame layout:
     * [rbp+16...] = arguments beyond 6 (if any)
     * [rbp+8]     = return address
     * [rbp]       = saved rbp
     * [rbp-8...]  = local variables
     *
     * Stack must be 16-byte aligned at call instructions.
     * After push %rbp, rsp is 16-byte aligned.
     */
    stack_size = n->offset;
    stack_size = align_to(stack_size, 16);

    /* prologue */
    emit_comment("prologue");
    emit("pushq %%rbp");
    emit("movq %%rsp, %%rbp");
    if (stack_size > 0) {
        emit("subq $%d, %%rsp", stack_size);
    }

    /* spill register arguments into their stack slots */
    i = 0;
    for (param = n->args; param != NULL && i < 6;
         param = param->next) {
        if (param->offset > 0) {
            emit_comment("save arg '%s' to [rbp-%d]",
                         param->name != NULL ? param->name : "?",
                         param->offset);
            if (param->ty != NULL && param->ty->size == 1) {
                emit("movb %%%sl, -%d(%%rbp)",
                     param_regs64[i], param->offset);
            } else if (param->ty != NULL && param->ty->size == 2) {
                /* need to use the 16-bit register name */
                emit("movw %%%s, -%d(%%rbp)",
                     param_regs64[i], param->offset);
            } else if (param->ty != NULL && param->ty->size == 4) {
                emit("movl %%%sd, -%d(%%rbp)",
                     param_regs64[i], param->offset);
            } else {
                emit("movq %%%s, -%d(%%rbp)",
                     param_regs64[i], param->offset);
            }
        }
        i++;
    }

    gen_x86_stmt(n->body);

    /* epilogue */
    emit_label(".L.return.%s", n->name);
    emit_comment("epilogue");
    emit("movq %%rbp, %%rsp");
    emit("popq %%rbp");
    emit("ret");

    fprintf(out, "\t.size %s, .-%s\n", n->name, n->name);
}

/* ---- global variable emission ---- */

static int p2align_for(int alignment)
{
    if (alignment >= 8) {
        return 3;
    }
    if (alignment >= 4) {
        return 2;
    }
    if (alignment >= 2) {
        return 1;
    }
    return 0;
}

static void gen_x86_globalvar(struct node *n)
{
    int sz;

    if (n == NULL || n->name == NULL) {
        return;
    }

    sz = (n->ty != NULL) ? n->ty->size : 8;

    fprintf(out, "\n");
    if (n->is_static) {
        fprintf(out, "\t.local %s\n", n->name);
    } else {
        fprintf(out, "\t.globl %s\n", n->name);
    }

    if (n->val != 0) {
        if (cc_data_sections) {
            fprintf(out,
                "\t.section .data.%s,\"aw\",@progbits\n",
                n->name);
        } else {
            fprintf(out, "\t.data\n");
        }
        if (n->ty != NULL && n->ty->align > 1) {
            fprintf(out, "\t.p2align %d\n",
                    p2align_for(n->ty->align));
        }
        emit_label("%s", n->name);
        if (sz == 1) {
            fprintf(out, "\t.byte %ld\n", n->val);
        } else if (sz == 2) {
            fprintf(out, "\t.short %ld\n", n->val);
        } else if (sz == 4) {
            fprintf(out, "\t.long %ld\n", n->val);
        } else {
            fprintf(out, "\t.quad %ld\n", n->val);
        }
    } else {
        if (cc_data_sections) {
            fprintf(out,
                "\t.section .bss.%s,\"aw\",@nobits\n",
                n->name);
        } else {
            fprintf(out, "\t.bss\n");
        }
        if (n->ty != NULL && n->ty->align > 1) {
            fprintf(out, "\t.p2align %d\n",
                    p2align_for(n->ty->align));
        }
        emit_label("%s", n->name);
        fprintf(out, "\t.zero %d\n", sz);
    }
}

/* ---- public interface ---- */

void gen_x86(struct node *prog, FILE *outfile)
{
    struct node *n;
    int need_rodata;

    out = outfile;
    label_count = 0;
    current_break_label = -1;
    current_continue_label = -1;

    fprintf(out, "/* generated by free-cc (x86_64) */\n");

    /* emit string literals in .rodata */
    need_rodata = 0;
    for (n = prog; n != NULL; n = n->next) {
        if (n->kind == ND_FUNCDEF || n->kind == ND_STR) {
            need_rodata = 1;
            break;
        }
    }
    if (need_rodata) {
        fprintf(out, "\n\t.section .rodata\n");
        collect_all_strings(prog);
    }

    /* emit global variables */
    for (n = prog; n != NULL; n = n->next) {
        if (n->kind == ND_VAR && n->offset == 0) {
            gen_x86_globalvar(n);
        }
    }

    /* emit functions */
    fprintf(out, "\n\t.text\n");
    for (n = prog; n != NULL; n = n->next) {
        if (n->kind == ND_FUNCDEF) {
            gen_x86_funcdef(n);
        }
    }

    fprintf(out, "\n\t.section .note.GNU-stack,\"\",@progbits\n");
}
