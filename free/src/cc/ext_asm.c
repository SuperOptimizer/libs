/*
 * ext_asm.c - GCC inline assembly support for free-cc.
 * Parses asm/__asm__/__asm with extended operand syntax.
 * Emits template with operand substitutions for aarch64.
 * Pure C89. All variables at top of block.
 */

#include "free.h"
#include <string.h>
#include <stdio.h>

#define ASM_MAX_OPERANDS 30
#define ASM_MAX_CLOBBERS 32
#define ASM_MAX_TEMPLATE 4096
#define ASM_MAX_GOTO_LABELS 16

/* set by asm_var_lookup in parse.c after each lookup call */
extern int asm_var_is_upvar;

/* forward declarations */
static int is_imm_constraint(const char *c);
static int is_mem_constraint(const char *c);

struct asm_operand {
    char constraint[16];    /* e.g. "=r", "r", "m", "i" */
    int is_output;
    int is_readwrite;       /* '+' constraint */
    char *sym_name;         /* [name] symbolic name */
    int var_offset;         /* stack offset for local var */
    int is_global;
    char *global_name;
    int is_addr_of;         /* 1 if expression is &var */
    int tied_to;            /* digit constraint: tied to output N, -1 = none */
    int is_imm;             /* 1 if "i" constraint (immediate) */
    long imm_val;           /* immediate value for "i" constraint */
    int is_upvar;           /* 1 if variable is from enclosing function scope */
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

extern struct tok *pp_next(void);

/*
 * Callback for evaluating constant expressions in "i"/"n" constraint
 * operands. Set by the parser before calling asm_parse().
 * The callback receives the current tok_ptr, parses an expression
 * from the token stream (consuming tokens up to but not including
 * the closing ')'), and returns the constant value.
 * Returns 1 on success, 0 on failure.
 */
static int (*asm_eval_const_cb)(struct tok **tok_ptr, long *out_val);

void asm_set_const_eval(int (*cb)(struct tok **tok_ptr, long *out_val))
{
    asm_eval_const_cb = cb;
}

static struct arena *asm_arena;
static struct tok *asm_tok;

static struct tok *asm_peek(void) { return asm_tok; }

static struct tok *asm_advance(void)
{
    struct tok *t;
    t = asm_tok;
    asm_tok = pp_next();
    return t;
}

static void asm_expect(enum tok_kind kind, const char *msg)
{
    if (asm_tok->kind != kind)
        err_at(asm_tok->file, asm_tok->line, asm_tok->col,
               "in asm: expected %s", msg);
    asm_tok = pp_next();
}

int asm_is_asm_keyword(const struct tok *t)
{
    if (t->kind != TOK_IDENT || !t->str) return 0;
    return strcmp(t->str, "asm") == 0 ||
           strcmp(t->str, "__asm__") == 0 ||
           strcmp(t->str, "__asm") == 0;
}

void asm_ext_init(struct arena *a) { asm_arena = a; }

struct asm_stmt *asm_alloc_stmt(struct arena *a)
{
    return (struct asm_stmt *)arena_alloc(a, sizeof(struct asm_stmt));
}

/* parse constraint string into operand */
static void parse_constraint(const char *str, struct asm_operand *op)
{
    const char *p;
    int i;
    p = str; i = 0;
    op->is_output = 0; op->is_readwrite = 0;
    if (*p == '=') { op->is_output = 1; p++; }
    else if (*p == '+') { op->is_output = 1; op->is_readwrite = 1; p++; }
    while (*p && i < 15) op->constraint[i++] = *p++;
    op->constraint[i] = '\0';
}

/* Collect adjacent string literals into one buffer.
 * Kernel asm macros commonly expand constraints as adjacent strings,
 * for example: __stringify(constraint) "r" (x). */
static void collect_concatenated_str(char *buf, int buf_size)
{
    int pos;
    int i;
    struct tok *t;

    pos = 0;
    if (buf_size > 0) {
        buf[0] = '\0';
    }
    while (asm_peek()->kind == TOK_STR) {
        t = asm_advance();
        for (i = 0; i < t->len && pos + 1 < buf_size; i++) {
            buf[pos++] = t->str[i];
        }
        if (buf_size > 0) {
            buf[pos] = '\0';
        }
    }
}

/* parse comma-separated operand list */
static int parse_operand_list(struct asm_operand *ops, int max_ops)
{
    int n;
    char constraint[64];
    n = 0;
    while (asm_peek()->kind != TOK_COLON &&
           asm_peek()->kind != TOK_RPAREN &&
           asm_peek()->kind != TOK_EOF && n < max_ops) {
        fprintf(stderr, "parse_operand_list op=%d start kind=%d at %s:%d:%d\n",
                n, asm_peek()->kind, asm_peek()->file, asm_peek()->line,
                asm_peek()->col);
        memset(&ops[n], 0, sizeof(struct asm_operand));
        /* optional [name] */
        if (asm_peek()->kind == TOK_LBRACKET) {
            asm_advance();
            if (asm_peek()->kind == TOK_IDENT) {
                ops[n].sym_name = str_dup(asm_arena,
                    asm_peek()->str, asm_peek()->len);
                asm_advance();
            }
            asm_expect(TOK_RBRACKET, "']'");
        }
        /* constraint string */
        if (asm_peek()->kind == TOK_STR) {
            collect_concatenated_str(constraint, sizeof(constraint));
            parse_constraint(constraint, &ops[n]);
        }
        /* check for digit constraint (tied operand) */
        ops[n].tied_to = -1;
        if (ops[n].constraint[0] >= '0' &&
            ops[n].constraint[0] <= '9') {
            ops[n].tied_to = ops[n].constraint[0] - '0';
        }
        /* (expr) - parse operand expression */
        if (asm_peek()->kind == TOK_LPAREN) {
            int used_cb;
            used_cb = 0;
            /*
             * For "i"/"n" constraints, try using the parser's
             * constant expression evaluator via callback.
             * This handles sizeof(), offsetof(), and other
             * compile-time constant expressions.
             */
            if (is_imm_constraint(ops[n].constraint) &&
                asm_eval_const_cb != NULL) {
                long cval;
                cval = 0;
                /* callback always consumes (expr), so mark
                 * used_cb=1 regardless of const eval success */
                used_cb = 1;
                fprintf(stderr, "parse_operand_list imm-cb op=%d constraint=%s tok=%d at %s:%d:%d\n",
                        n, ops[n].constraint, asm_peek()->kind,
                        asm_peek()->file, asm_peek()->line, asm_peek()->col);
                if (asm_eval_const_cb(&asm_tok, &cval)) {
                    ops[n].is_imm = 1;
                    ops[n].imm_val = cval;
                    fprintf(stderr, "parse_operand_list imm-cb ok op=%d val=%ld next=%d at %s:%d:%d\n",
                            n, cval, asm_peek()->kind,
                            asm_peek()->file, asm_peek()->line,
                            asm_peek()->col);
                } else {
                    fprintf(stderr, "parse_operand_list imm-cb fail op=%d next=%d at %s:%d:%d\n",
                            n, asm_peek()->kind, asm_peek()->file,
                            asm_peek()->line, asm_peek()->col);
                }
            }
            if (!used_cb) {
                int depth;
                asm_advance();
                depth = 1;
                /* check for &var (address-of) */
                if (asm_peek()->kind == TOK_AMP) {
                    ops[n].is_addr_of = 1;
                    asm_advance();
                }
                /* check for numeric immediate */
                if (asm_peek()->kind == TOK_NUM) {
                    ops[n].is_imm = 1;
                    ops[n].imm_val = asm_peek()->val;
                }
                /* check for negative numeric immediate */
                if (asm_peek()->kind == TOK_MINUS &&
                    depth == 1) {
                    struct tok *sign_tok;
                    sign_tok = asm_advance();
                    (void)sign_tok;
                    if (asm_peek()->kind == TOK_NUM) {
                        ops[n].is_imm = 1;
                        ops[n].imm_val = -asm_peek()->val;
                    }
                }
                if (asm_peek()->kind == TOK_IDENT) {
                    ops[n].global_name = str_dup(asm_arena,
                        asm_peek()->str, asm_peek()->len);
                    ops[n].is_global = 1;
                }
                while (depth > 0 &&
                       asm_peek()->kind != TOK_EOF) {
                    if (asm_peek()->kind == TOK_LPAREN)
                        depth++;
                    else if (asm_peek()->kind == TOK_RPAREN) {
                        depth--;
                        if (depth == 0) {
                            asm_advance();
                            break;
                        }
                    }
                    asm_advance();
                }
            }
        }
        n++;
        if (asm_peek()->kind == TOK_COMMA) asm_advance();
    }
    return n;
}

/* parse comma-separated clobber list */
static int parse_clobber_list(char **clob, int max)
{
    int n;
    char clobber[64];
    n = 0;
    while (asm_peek()->kind != TOK_RPAREN &&
           asm_peek()->kind != TOK_COLON &&
           asm_peek()->kind != TOK_EOF && n < max) {
        if (asm_peek()->kind == TOK_STR) {
            collect_concatenated_str(clobber, sizeof(clobber));
            clob[n++] = str_dup(asm_arena, clobber,
                                (int)strlen(clobber));
        } else asm_advance();
        if (asm_peek()->kind == TOK_COMMA) asm_advance();
    }
    return n;
}

/*
 * asm_parse - parse inline asm statement.
 * Grammar: asm [volatile] [goto] ( template
 *   [: outputs [: inputs [: clobbers [: goto_labels]]]] );
 */
void asm_parse(struct asm_stmt *stmt, struct tok **tok_ptr)
{
    struct tok *t;
    int pos;

    asm_tok = *tok_ptr;
    memset(stmt, 0, sizeof(struct asm_stmt));
    /* asm keyword already consumed by caller */

    /* optional volatile */
    if (asm_peek()->kind == TOK_VOLATILE ||
        (asm_peek()->kind == TOK_IDENT && asm_peek()->str &&
         (strcmp(asm_peek()->str, "__volatile__") == 0 ||
          strcmp(asm_peek()->str, "volatile") == 0))) {
        stmt->is_volatile = 1; asm_advance();
    }
    /* optional goto */
    if (asm_peek()->kind == TOK_GOTO ||
        (asm_peek()->kind == TOK_IDENT && asm_peek()->str &&
         strcmp(asm_peek()->str, "goto") == 0)) {
        stmt->is_goto = 1; asm_advance();
    }

    asm_expect(TOK_LPAREN, "'(' after asm");

    /* template string (may be concatenated) */
    pos = 0;
    while (asm_peek()->kind == TOK_STR) {
        int remain;
        t = asm_advance();
        fprintf(stderr,
                "asm_parse template tok kind=%d str=%s at %s:%d:%d\n",
                t->kind, t->str ? t->str : "(null)",
                t->file, t->line, t->col);
        if (t->str && t->len > 0) {
            remain = ASM_MAX_TEMPLATE - pos - 1;
            if (t->len < remain) remain = t->len;
            memcpy(stmt->template_str + pos, t->str, (size_t)remain);
            pos += remain;
        }
    }
    stmt->template_str[pos] = '\0';
    fprintf(stderr, "asm_parse after template kind=%d at %s:%d:%d\n",
            asm_peek()->kind, asm_peek()->file, asm_peek()->line,
            asm_peek()->col);

    /* : outputs */
    if (asm_peek()->kind == TOK_COLON) {
        fprintf(stderr, "asm_parse outputs colon at %s:%d:%d\n",
                asm_peek()->file, asm_peek()->line, asm_peek()->col);
        asm_advance();
        stmt->noutputs = parse_operand_list(stmt->outputs,
                                            ASM_MAX_OPERANDS);
    }
    /* : inputs */
    if (asm_peek()->kind == TOK_COLON) {
        fprintf(stderr, "asm_parse inputs colon at %s:%d:%d\n",
                asm_peek()->file, asm_peek()->line, asm_peek()->col);
        asm_advance();
        stmt->ninputs = parse_operand_list(stmt->inputs,
                                           ASM_MAX_OPERANDS);
    }
    /* : clobbers */
    if (asm_peek()->kind == TOK_COLON) {
        fprintf(stderr, "asm_parse clobbers colon at %s:%d:%d\n",
                asm_peek()->file, asm_peek()->line, asm_peek()->col);
        asm_advance();
        stmt->nclobbers = parse_clobber_list(stmt->clobbers,
                                             ASM_MAX_CLOBBERS);
    }
    /* : goto labels */
    if (asm_peek()->kind == TOK_COLON) {
        fprintf(stderr, "asm_parse gotolabels colon at %s:%d:%d\n",
                asm_peek()->file, asm_peek()->line, asm_peek()->col);
        asm_advance();
        stmt->ngoto_labels = 0;
        while (asm_peek()->kind == TOK_IDENT &&
               stmt->ngoto_labels < ASM_MAX_GOTO_LABELS) {
            struct tok *lt;
            lt = asm_advance();
            stmt->goto_labels[stmt->ngoto_labels++] =
                str_dup(asm_arena, lt->str, lt->len);
            if (asm_peek()->kind == TOK_COMMA)
                asm_advance();
        }
    }
    if (asm_peek()->kind == TOK_RPAREN) asm_advance();
    if (asm_peek()->kind == TOK_SEMI) asm_advance();
    *tok_ptr = asm_tok;
}

/* ---- code emission ---- */

/* find operand index by symbolic name */
static int find_named_op(const struct asm_stmt *stmt,
                          const char *name, int len)
{
    int i;
    for (i = 0; i < stmt->noutputs; i++)
        if (stmt->outputs[i].sym_name &&
            (int)strlen(stmt->outputs[i].sym_name) == len &&
            strncmp(stmt->outputs[i].sym_name, name, (size_t)len) == 0)
            return i;
    for (i = 0; i < stmt->ninputs; i++)
        if (stmt->inputs[i].sym_name &&
            (int)strlen(stmt->inputs[i].sym_name) == len &&
            strncmp(stmt->inputs[i].sym_name, name, (size_t)len) == 0)
            return stmt->noutputs + i;
    return 0;
}

/* find operand struct by symbolic name, or by index */
static const struct asm_operand *find_operand(
    const struct asm_stmt *stmt, int idx)
{
    if (idx < stmt->noutputs)
        return &stmt->outputs[idx];
    idx -= stmt->noutputs;
    if (idx < stmt->ninputs)
        return &stmt->inputs[idx];
    return NULL;
}

/* check if constraint indicates an immediate operand */
static int is_imm_constraint(const char *c)
{
    while (*c) {
        if (*c == 'i' || *c == 'n') return 1;
        c++;
    }
    return 0;
}

/* check if constraint indicates a memory operand */
static int is_mem_constraint(const char *c)
{
    while (*c) {
        if (*c == 'm' || *c == 'Q') return 1;
        c++;
    }
    return 0;
}

/*
 * emit_operand_ref - emit an operand reference with optional modifier.
 * modifier: 0 = default (x-register), 'w' = w-register, 'x' = x-register,
 *           's' = s-register, 'c' = bare constant (no prefix).
 */
static int asm_operand_reg(const struct asm_stmt *stmt,
                            int input_base, int idx, int has_casp)
{
    int in_idx;
    if (has_casp) {
        switch (idx) {
        case 0:
            return 0;
        case 1:
            return 1;
        case 2:
            return 4;
        case 3:
            return 2;
        case 4:
            return 3;
        default:
            return idx;
        }
    }
    if (idx < stmt->noutputs) return idx;
    in_idx = idx - stmt->noutputs;
    if (in_idx >= 0 && in_idx < stmt->ninputs) {
        if (stmt->inputs[in_idx].tied_to >= 0)
            return stmt->inputs[in_idx].tied_to;
        return input_base + in_idx;
    }
    return idx;
}

static void emit_operand_ref(FILE *out, const struct asm_stmt *stmt,
                              int input_base, int idx, int modifier,
                              int has_casp)
{
    const struct asm_operand *op;
    int reg;
    op = find_operand(stmt, idx);
    reg = asm_operand_reg(stmt, input_base, idx, has_casp);
    if (modifier == 'c') {
        /* %c: bare constant - emit immediate value or symbol name */
        if (op && op->is_imm) {
            fprintf(out, "%ld", op->imm_val);
        } else if (op && op->global_name) {
            fprintf(out, "%s", op->global_name);
        } else {
            /* best-effort fallback for non-constant immediate operands */
            fprintf(out, "0");
        }
        return;
    }
    if (op && is_imm_constraint(op->constraint)) {
        if (op->is_imm) {
            fprintf(out, "%ld", op->imm_val);
        } else if (op->global_name) {
            fprintf(out, "%s", op->global_name);
        } else {
            /* Non-constant "i"/"n" operands are not valid immediates.
             * Emit a constant placeholder so the assembly still parses. */
            fprintf(out, "0");
        }
    } else if (op && is_mem_constraint(op->constraint)) {
        /* memory operand: emit [xN] */
        fprintf(out, "[x%d]", reg);
    } else if (modifier == 'w') {
        fprintf(out, "w%d", reg);
    } else if (modifier == 's') {
        fprintf(out, "s%d", reg);
    } else {
        /* default or explicit 'x' modifier */
        fprintf(out, "x%d", reg);
    }
}

/*
 * emit_load_operand - load an operand value into register xN.
 * For globals: adrp/add, then ldr unless is_addr_of.
 * For locals:  ldr from [x29, #-offset], or sub for addr_of.
 */
/*
 * emit_sub_offset_base - emit instructions to compute base_reg - offset
 * into xN. Handles offsets that don't fit in a 12-bit immediate.
 */
static void emit_sub_offset_base(FILE *out, int reg, int offset,
                                  const char *base)
{
    if (offset <= 4095) {
        fprintf(out, "\tsub x%d, %s, #%d\n", reg, base, offset);
    } else if (offset <= 65535) {
        fprintf(out, "\tmov x%d, #%d\n", reg, offset);
        fprintf(out, "\tsub x%d, %s, x%d\n", reg, base, reg);
    } else {
        fprintf(out, "\tmovz x%d, #%d\n", reg, offset & 0xffff);
        fprintf(out, "\tmovk x%d, #%d, lsl #16\n", reg,
                (offset >> 16) & 0xffff);
        fprintf(out, "\tsub x%d, %s, x%d\n", reg, base, reg);
    }
}

static void emit_load_operand(FILE *out,
                               const struct asm_operand *op, int reg)
{
    /* immediate constraint: load the constant value directly */
    if (op->is_imm && is_imm_constraint(op->constraint)) {
        fprintf(out, "\tmov x%d, #%ld\n", reg, op->imm_val);
        return;
    }
    if (op->var_offset > 0) {
        /* local variable (or upvar via x19) */
        const char *base;
        base = op->is_upvar ? "x19" : "x29";
        if (op->is_addr_of || is_mem_constraint(op->constraint)) {
            /* address-of or memory constraint: load address only */
            emit_sub_offset_base(out, reg, op->var_offset, base);
        } else if (op->var_offset <= 255) {
            fprintf(out, "\tldur x%d, [%s, #-%d]\n",
                    reg, base, op->var_offset);
        } else {
            /* offset too large for immediate: compute address first */
            emit_sub_offset_base(out, reg, op->var_offset, base);
            fprintf(out, "\tldr x%d, [x%d]\n", reg, reg);
        }
    } else if (op->is_global && op->global_name) {
        /* global variable */
        fprintf(out, "\tadrp x%d, %s\n",
                reg, op->global_name);
        fprintf(out, "\tadd x%d, x%d, :lo12:%s\n",
                reg, reg, op->global_name);
        if (!op->is_addr_of && !is_mem_constraint(op->constraint)) {
            fprintf(out, "\tldr x%d, [x%d]\n", reg, reg);
        }
    }
}

/*
 * emit_store_operand - store register xN into an operand.
 */
static void emit_store_operand(FILE *out,
                                const struct asm_operand *op, int reg)
{
    if (op->var_offset > 0) {
        const char *base;
        base = op->is_upvar ? "x19" : "x29";
        if (op->var_offset <= 255) {
            fprintf(out, "\tstur x%d, [%s, #-%d]\n",
                    reg, base, op->var_offset);
        } else {
            /* offset too large: compute address in x9, then store */
            emit_sub_offset_base(out, 9, op->var_offset, base);
            fprintf(out, "\tstr x%d, [x9]\n", reg);
        }
    } else if (op->is_global && op->global_name) {
        fprintf(out, "\tadrp x9, %s\n", op->global_name);
        fprintf(out, "\tadd x9, x9, :lo12:%s\n",
                op->global_name);
        fprintf(out, "\tstr x%d, [x9]\n", reg);
    }
}

void asm_emit(FILE *out, const struct asm_stmt *stmt,
              const char *func_name)
{
    const char *p;
    int total_ops;
    int i;
    int op_idx;
    int input_base;
    int has_casp;

    total_ops = stmt->noutputs + stmt->ninputs;
    has_casp = stmt->template_str[0] != '\0' &&
        strstr(stmt->template_str, "casp") != NULL;
    input_base = stmt->noutputs;
    if (has_casp && (input_base & 1)) {
        input_base++;
    }

    /* load read-write ("+r") output operands before template */
    for (i = 0; i < stmt->noutputs; i++) {
        if (stmt->outputs[i].is_readwrite) {
            emit_load_operand(out, &stmt->outputs[i],
                              asm_operand_reg(stmt, input_base, i,
                                              has_casp));
        }
    }

    /* load input operands into registers.
     * For tied constraints ("0", "1", etc.), load the input
     * into the SAME register as the tied output. */
    for (i = 0; i < stmt->ninputs; i++) {
        int reg;
        reg = asm_operand_reg(stmt, input_base, stmt->noutputs + i,
                              has_casp);
        emit_load_operand(out, &stmt->inputs[i], reg);
    }

    /* emit template with operand substitution */
    if (stmt->template_str[0] != '\0') {
        fprintf(out, "\t/* inline asm */\n\t");
        p = stmt->template_str;
        while (*p) {
            if (*p == '%' && *(p + 1) == '%') {
                fputc('%', out); p += 2;
            } else if (*p == '%' && *(p + 1) == 'l' &&
                       *(p + 2) == '[') {
                /* asm goto: %l[label_name] */
                const char *start;
                int len;
                p += 3; start = p;
                while (*p && *p != ']') p++;
                len = (int)(p - start);
                if (*p == ']') p++;
                /* find label in goto_labels list */
                {
                    int found;
                    found = 0;
                    for (i = 0; i < stmt->ngoto_labels; i++) {
                        if (stmt->goto_labels[i] &&
                            (int)strlen(stmt->goto_labels[i])
                            == len &&
                            strncmp(stmt->goto_labels[i],
                                    start, (size_t)len) == 0) {
                            found = 1;
                            break;
                        }
                    }
                    if (found && func_name) {
                        fprintf(out, ".L.label.%s.%.*s",
                                func_name, len, start);
                    } else {
                        /* fallback: emit as label directly */
                        fprintf(out, ".L.label.%s.%.*s",
                                func_name ? func_name : "_",
                                len, start);
                    }
                }
            } else if (*p == '%' && *(p + 1) == 'l' &&
                       *(p + 2) >= '0' && *(p + 2) <= '9') {
                /* asm goto: %lN where N is label index
                 * (offset from total operands) */
                int label_idx;
                p += 2;
                label_idx = 0;
                while (*p >= '0' && *p <= '9')
                    label_idx = label_idx * 10 + (*p++ - '0');
                /* label_idx is absolute; subtract total operands
                 * to get goto_labels index */
                label_idx -= total_ops;
                if (label_idx >= 0 &&
                    label_idx < stmt->ngoto_labels &&
                    stmt->goto_labels[label_idx]) {
                    fprintf(out, ".L.label.%s.%s",
                            func_name ? func_name : "_",
                            stmt->goto_labels[label_idx]);
                } else {
                    fprintf(out, ".L.label.unknown");
                }
            } else if (*p == '%' && (*(p + 1) == 'c' ||
                       *(p + 1) == 'w' || *(p + 1) == 'x' ||
                       *(p + 1) == 's') &&
                       *(p + 2) == '[') {
                /* %c[name], %w[name], %x[name], %s[name] */
                int mod;
                int named_idx;
                const char *start;
                int len;
                mod = *(p + 1);
                p += 3; start = p;
                while (*p && *p != ']') p++;
                len = (int)(p - start);
                if (*p == ']') p++;
                named_idx = find_named_op(stmt, start, len);
                emit_operand_ref(out, stmt, input_base, named_idx, mod,
                                 has_casp);
            } else if (*p == '%' && (*(p + 1) == 'c' ||
                       *(p + 1) == 'w' || *(p + 1) == 'x' ||
                       *(p + 1) == 's') &&
                       *(p + 2) >= '0' && *(p + 2) <= '9') {
                /* %c0, %w0, %x0, %s0 - modifiers */
                int mod;
                mod = *(p + 1);
                p += 2;
                op_idx = 0;
                while (*p >= '0' && *p <= '9')
                    op_idx = op_idx * 10 + (*p++ - '0');
                if (op_idx < total_ops)
                    emit_operand_ref(out, stmt, input_base, op_idx, mod,
                                     has_casp);
                else fprintf(out, "0");
            } else if (*p == '%' && (*(p + 1) == '[' ||
                       (*(p + 1) == ' ' &&
                        *(p + 2) == '['))) {
                const char *start;
                int len;
                int named_idx;
                p++; /* skip % */
                if (*p == ' ') p++; /* skip optional space */
                p++; /* skip [ */
                start = p;
                while (*p && *p != ']') p++;
                len = (int)(p - start);
                if (*p == ']') p++;
                named_idx = find_named_op(stmt, start, len);
                emit_operand_ref(out, stmt, input_base, named_idx, 0,
                                 has_casp);
            } else if (*p == '%' && *(p + 1) >= '0' &&
                       *(p + 1) <= '9') {
                op_idx = 0; p++;
                while (*p >= '0' && *p <= '9')
                    op_idx = op_idx * 10 + (*p++ - '0');
                if (op_idx < total_ops)
                    emit_operand_ref(out, stmt, input_base, op_idx, 0,
                                     has_casp);
                else fprintf(out, "0");
            } else if (*p == '%' && *(p + 1) == ' ' &&
                       *(p + 2) >= '0' && *(p + 2) <= '9') {
                /* handle "% 0" produced by stringify of
                 * separate % and digit tokens */
                op_idx = 0; p += 2;
                while (*p >= '0' && *p <= '9')
                    op_idx = op_idx * 10 + (*p++ - '0');
                if (op_idx < total_ops)
                    emit_operand_ref(out, stmt, input_base, op_idx, 0,
                                     has_casp);
                else fprintf(out, "0");
            } else if (*p == '\n' || *p == ';') {
                fprintf(out, "\n\t"); p++;
            } else {
                fputc(*p++, out);
            }
        }
        fprintf(out, "\n");
    } else {
        fprintf(out, "\t/* inline asm (empty) */\n");
    }

    /* store outputs back (skip memory operands: asm writes them directly) */
    for (i = 0; i < stmt->noutputs; i++) {
        if (is_mem_constraint(stmt->outputs[i].constraint))
            continue;
        emit_store_operand(out, &stmt->outputs[i],
                           asm_operand_reg(stmt, input_base, i,
                                           has_casp));
    }
}

void asm_emit_basic(FILE *out, const char *tmpl)
{
    const char *p;
    p = tmpl;
    fprintf(out, "\t/* basic inline asm */\n\t");
    while (*p) {
        if (*p == '\n' || *p == ';') fprintf(out, "\n\t");
        else fputc(*p, out);
        p++;
    }
    fprintf(out, "\n");
}

int asm_is_basic(const struct asm_stmt *stmt)
{
    return stmt->noutputs == 0 && stmt->ninputs == 0 &&
           stmt->nclobbers == 0;
}

/*
 * asm_resolve_operands - resolve variable names to stack offsets
 * using a caller-provided lookup function.  lookup(name) returns
 * the positive frame offset for a local variable, or 0 if the
 * name is a global.
 */
void asm_resolve_operands(struct asm_stmt *stmt,
                          int (*lookup)(const char *name))
{
    int i;
    int off;
    for (i = 0; i < stmt->noutputs; i++) {
        if (stmt->outputs[i].global_name) {
            asm_var_is_upvar = 0;
            off = lookup(stmt->outputs[i].global_name);
            if (off > 0) {
                stmt->outputs[i].var_offset = off;
                stmt->outputs[i].is_global = 0;
                stmt->outputs[i].is_upvar = asm_var_is_upvar;
            }
        }
    }
    for (i = 0; i < stmt->ninputs; i++) {
        if (stmt->inputs[i].global_name) {
            asm_var_is_upvar = 0;
            off = lookup(stmt->inputs[i].global_name);
            if (off > 0) {
                stmt->inputs[i].var_offset = off;
                stmt->inputs[i].is_global = 0;
                stmt->inputs[i].is_upvar = asm_var_is_upvar;
            }
        }
    }
}
