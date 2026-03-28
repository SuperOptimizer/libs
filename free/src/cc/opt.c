/*
 * opt.c - Peephole optimizer for the free C compiler.
 * Processes GAS-compatible aarch64 assembly text, rewriting
 * wasteful instruction sequences before they reach the assembler.
 * Multi-pass: runs patterns until no more changes occur.
 * Pure C89. No external dependencies beyond libc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "free.h"

/* ---- constants ---- */
#define MAX_LINE_LEN 256
#define MAX_OPERANDS 4
#define MAX_PASSES   16

/* ---- parsed assembly line ---- */
struct asm_line {
    char text[MAX_LINE_LEN];
    char mnemonic[32];
    char operands[MAX_OPERANDS][64];
    int noperands;
    int is_label;
    int is_directive;
    int is_comment;
    int removed;
    const char *long_text; /* original text for oversized lines */
    int long_len;          /* length of long_text, 0 if fits in text[] */
};

/* ---- forward declarations ---- */
static void parse_line(struct asm_line *l);
static int streq(const char *a, const char *b);
static int is_power_of_two(long v, int *shift_out);
static long parse_imm(const char *s);
static int has_imm_prefix(const char *s);
static int is_q_reg(const char *reg);

/* ---- string helpers ---- */

static int streq(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

/*
 * skip_ws - return pointer past leading whitespace.
 */
static const char *skip_ws(const char *s)
{
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    return s;
}

/*
 * strip_trailing - remove trailing whitespace and newline in place.
 */
static void strip_trailing(char *s)
{
    int len;

    len = (int)strlen(s);
    while (len > 0 &&
           (s[len - 1] == '\n' || s[len - 1] == '\r' ||
            s[len - 1] == ' '  || s[len - 1] == '\t')) {
        s[len - 1] = '\0';
        len--;
    }
}

static int is_q_reg(const char *reg)
{
    return reg != NULL && reg[0] == 'q';
}

/* ---- immediate value parsing ---- */

/*
 * has_imm_prefix - check if operand starts with # (immediate).
 */
static int has_imm_prefix(const char *s)
{
    return s[0] == '#';
}

/*
 * parse_imm - parse an immediate value from "#N" or "#0xN".
 * Returns the value, or 0 on failure.
 */
static long parse_imm(const char *s)
{
    if (s[0] == '#') {
        s++;
    }
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        return strtol(s, NULL, 16);
    }
    return strtol(s, NULL, 10);
}

/*
 * is_power_of_two - check if v is a positive power of two.
 * If so, store the shift amount in *shift_out and return 1.
 */
static int is_power_of_two(long v, int *shift_out)
{
    int n;

    if (v <= 0) {
        return 0;
    }
    n = 0;
    while (v > 1) {
        if (v & 1) {
            return 0;
        }
        v >>= 1;
        n++;
    }
    *shift_out = n;
    return 1;
}

/* ---- line parsing ---- */

/*
 * parse_line - minimally parse an assembly line.
 * Extracts mnemonic and operands from instruction lines.
 * Identifies labels, directives, and comments.
 */
static void parse_line(struct asm_line *l)
{
    const char *p;
    const char *start;
    int len;
    int i;
    int depth;

    l->mnemonic[0] = '\0';
    l->noperands = 0;
    l->is_label = 0;
    l->is_directive = 0;
    l->is_comment = 0;
    for (i = 0; i < MAX_OPERANDS; i++) {
        l->operands[i][0] = '\0';
    }

    p = skip_ws(l->text);

    /* empty line */
    if (*p == '\0') {
        return;
    }

    /* comment line: starts with / * or // */
    if (p[0] == '/' && p[1] == '*') {
        l->is_comment = 1;
        return;
    }

    /* inline comment emitted by gen.c: \t/ * ... * / */
    if (*p == '\0') {
        return;
    }

    /* label: first token ends with ':', not inside brackets/operands */
    {
        const char *t;

        t = p;
        /* scan the first token (label candidate) */
        while (*t && *t != ':' && *t != ' ' && *t != '\t' && *t != '\n') {
            t++;
        }
        if (*t == ':' && t > p) {
            /* ensure the colon terminates the token (not :lo12: mid-line) */
            if (*(t + 1) == '\0' || *(t + 1) == '\n' || *(t + 1) == ' '
                || *(t + 1) == '\t') {
                l->is_label = 1;
                return;
            }
        }
    }

    /* directive: starts with '.' */
    if (*p == '.') {
        l->is_directive = 1;
        return;
    }

    /* instruction: extract mnemonic */
    start = p;
    while (*p && *p != ' ' && *p != '\t' && *p != '\n') {
        p++;
    }
    len = (int)(p - start);
    if (len >= (int)sizeof(l->mnemonic)) {
        len = (int)sizeof(l->mnemonic) - 1;
    }
    memcpy(l->mnemonic, start, (size_t)len);
    l->mnemonic[len] = '\0';

    /* extract operands, split by comma, respect brackets */
    p = skip_ws(p);
    i = 0;
    while (*p && *p != '\n' && i < MAX_OPERANDS) {
        /* skip comment at end of line */
        if (p[0] == '/' && p[1] == '*') {
            break;
        }

        start = p;
        depth = 0;
        while (*p && *p != '\n') {
            if (*p == '[') {
                depth++;
            } else if (*p == ']') {
                depth--;
            } else if (*p == ',' && depth == 0) {
                break;
            } else if (p[0] == '/' && p[1] == '*') {
                break;
            }
            p++;
        }

        /* trim trailing whitespace from operand */
        len = (int)(p - start);
        while (len > 0 && (start[len - 1] == ' ' ||
                           start[len - 1] == '\t')) {
            len--;
        }
        if (len > 0 && len < 64) {
            memcpy(l->operands[i], start, (size_t)len);
            l->operands[i][len] = '\0';
            i++;
        }

        if (*p == ',') {
            p++;
            p = skip_ws(p);
        }
    }
    l->noperands = i;
}

/* ---- optimization patterns ---- */

/*
 * Pattern 1: Push/pop elimination (stack machine artifact).
 *   str x0, [sp, #-16]!    (push x0)
 *   ldr xN, [sp], #16      (pop to xN)
 *   => mov xN, x0           if N != 0
 *   => remove both           if N == 0
 */
static int opt_push_pop(struct asm_line *lines, int n)
{
    int changed;
    int i;

    changed = 0;
    for (i = 0; i < n - 1; i++) {
        if (lines[i].removed || lines[i + 1].removed) {
            continue;
        }
        if (!streq(lines[i].mnemonic, "str") ||
            !streq(lines[i + 1].mnemonic, "ldr")) {
            continue;
        }
        /* check push pattern: str xR, [sp, #-16]! */
        if (lines[i].noperands < 2) {
            continue;
        }
        if (!streq(lines[i].operands[1], "[sp, #-16]!")) {
            continue;
        }
        /* check pop pattern: ldr xR, [sp], #16 */
        if (lines[i + 1].noperands < 2) {
            continue;
        }
        if (!streq(lines[i + 1].operands[1], "[sp]") ||
            lines[i + 1].noperands < 3 ||
            !streq(lines[i + 1].operands[2], "#16")) {
            continue;
        }

        if (streq(lines[i].operands[0], lines[i + 1].operands[0])) {
            /* push then pop to same register: both are no-ops */
            lines[i].removed = 1;
            lines[i + 1].removed = 1;
            changed = 1;
        } else {
            /* push xA then pop xB: replace with mov xB, xA
             * Use fmov for FP registers (d/s prefix) */
            const char *mne;
            if (is_q_reg(lines[i].operands[0]) ||
                is_q_reg(lines[i + 1].operands[0])) {
                continue;
            }
            lines[i].removed = 1;
            mne = (lines[i].operands[0][0] == 'd' ||
                   lines[i].operands[0][0] == 's') ?
                   "fmov" : "mov";
            sprintf(lines[i + 1].text, "\t%s %s, %s",
                    mne,
                    lines[i + 1].operands[0],
                    lines[i].operands[0]);
            parse_line(&lines[i + 1]);
            changed = 1;
        }
    }
    return changed;
}

/*
 * Pattern 2: Redundant load after store.
 *   str xR, [x29, #-N]
 *   ldr xR, [x29, #-N]     immediately after — remove the ldr
 */
static int opt_store_load(struct asm_line *lines, int n)
{
    int changed;
    int i;

    changed = 0;
    for (i = 0; i < n - 1; i++) {
        if (lines[i].removed || lines[i + 1].removed) {
            continue;
        }
        /* match str followed by ldr with same register and address */
        if (streq(lines[i].mnemonic, "str") &&
            streq(lines[i + 1].mnemonic, "ldr") &&
            lines[i].noperands >= 2 &&
            lines[i + 1].noperands >= 2 &&
            streq(lines[i].operands[0], lines[i + 1].operands[0]) &&
            streq(lines[i].operands[1], lines[i + 1].operands[1])) {
            /* also match strb/ldrb, strh/ldrh */
            lines[i + 1].removed = 1;
            changed = 1;
        }
        /* also match size-suffixed variants */
        if (streq(lines[i].mnemonic, "strb") &&
            streq(lines[i + 1].mnemonic, "ldrb") &&
            lines[i].noperands >= 2 &&
            lines[i + 1].noperands >= 2 &&
            streq(lines[i].operands[0], lines[i + 1].operands[0]) &&
            streq(lines[i].operands[1], lines[i + 1].operands[1])) {
            lines[i + 1].removed = 1;
            changed = 1;
        }
        if (streq(lines[i].mnemonic, "strh") &&
            streq(lines[i + 1].mnemonic, "ldrh") &&
            lines[i].noperands >= 2 &&
            lines[i + 1].noperands >= 2 &&
            streq(lines[i].operands[0], lines[i + 1].operands[0]) &&
            streq(lines[i].operands[1], lines[i + 1].operands[1])) {
            lines[i + 1].removed = 1;
            changed = 1;
        }
    }
    return changed;
}

/*
 * Pattern 3: Redundant self-move.
 *   mov xN, xN              — remove
 */
static int opt_self_move(struct asm_line *lines, int n)
{
    int changed;
    int i;

    changed = 0;
    for (i = 0; i < n; i++) {
        if (lines[i].removed) {
            continue;
        }
        if (streq(lines[i].mnemonic, "mov") &&
            lines[i].noperands >= 2 &&
            streq(lines[i].operands[0], lines[i].operands[1])) {
            /* mov wN, wN is NOT a nop: it zero-extends,
             * clearing the upper 32 bits of xN.
             * Only remove mov xN, xN (true self-move). */
            if (lines[i].operands[0][0] == 'x') {
                lines[i].removed = 1;
                changed = 1;
            }
        }
    }
    return changed;
}

/*
 * Pattern 4: Add/sub zero elimination.
 *   add xR, xR, #0          — remove
 *   sub xR, xR, #0          — remove
 */
static int opt_addsub_zero(struct asm_line *lines, int n)
{
    int changed;
    int i;

    changed = 0;
    for (i = 0; i < n; i++) {
        if (lines[i].removed) {
            continue;
        }
        if ((streq(lines[i].mnemonic, "add") ||
             streq(lines[i].mnemonic, "sub")) &&
            lines[i].noperands >= 3 &&
            streq(lines[i].operands[0], lines[i].operands[1]) &&
            has_imm_prefix(lines[i].operands[2]) &&
            parse_imm(lines[i].operands[2]) == 0) {
            lines[i].removed = 1;
            changed = 1;
        }
    }
    return changed;
}

/*
 * Pattern 5: Dead code elimination.
 *   mov xR, #A
 *   mov xR, #B              — remove the first mov
 *
 * Only applies when the register is the same and the first result
 * is never used (immediate overwrite).
 */
static int opt_dead_mov(struct asm_line *lines, int n)
{
    int changed;
    int i;

    changed = 0;
    for (i = 0; i < n - 1; i++) {
        if (lines[i].removed || lines[i + 1].removed) {
            continue;
        }
        if (streq(lines[i].mnemonic, "mov") &&
            streq(lines[i + 1].mnemonic, "mov") &&
            lines[i].noperands >= 2 &&
            lines[i + 1].noperands >= 2 &&
            has_imm_prefix(lines[i].operands[1]) &&
            has_imm_prefix(lines[i + 1].operands[1]) &&
            streq(lines[i].operands[0], lines[i + 1].operands[0])) {
            lines[i].removed = 1;
            changed = 1;
        }
    }
    return changed;
}

/*
 * Pattern 6: Branch to next instruction.
 *   b .Lxx
 *   .Lxx:                   — remove the branch
 */
static int opt_branch_next(struct asm_line *lines, int n)
{
    int changed;
    int i;
    int j;
    const char *target;
    const char *p;
    int tlen;
    char label_buf[MAX_LINE_LEN];

    changed = 0;
    for (i = 0; i < n - 1; i++) {
        if (lines[i].removed) {
            continue;
        }
        if (!streq(lines[i].mnemonic, "b") || lines[i].noperands < 1) {
            continue;
        }

        target = lines[i].operands[0];

        /* find the next non-removed line */
        for (j = i + 1; j < n; j++) {
            if (!lines[j].removed) {
                break;
            }
        }
        if (j >= n) {
            continue;
        }

        /* check if next line is the target label */
        if (!lines[j].is_label) {
            continue;
        }

        /* extract label name from the line text (strip colon) */
        p = skip_ws(lines[j].text);
        tlen = 0;
        while (p[tlen] && p[tlen] != ':' && p[tlen] != '\n' &&
               p[tlen] != ' ') {
            tlen++;
        }
        if (tlen >= MAX_LINE_LEN) {
            tlen = MAX_LINE_LEN - 1;
        }
        memcpy(label_buf, p, (size_t)tlen);
        label_buf[tlen] = '\0';

        if (streq(target, label_buf)) {
            lines[i].removed = 1;
            changed = 1;
        }
    }
    return changed;
}

/*
 * Pattern 7: Constant folding.
 *   mov x0, #A
 *   mov x1, #B
 *   add x0, x0, x1          => mov x0, #(A+B)
 *   sub x0, x0, x1          => mov x0, #(A-B)
 *   mul x0, x0, x1          => mov x0, #(A*B)
 *
 * Only folds when both immediates fit in a simple mov.
 */
static int opt_const_fold(struct asm_line *lines, int n)
{
    int changed;
    int i;
    long a;
    long b;
    long result;

    changed = 0;
    for (i = 0; i < n - 2; i++) {
        if (lines[i].removed || lines[i + 1].removed ||
            lines[i + 2].removed) {
            continue;
        }
        /* mov x0, #A */
        if (!streq(lines[i].mnemonic, "mov") ||
            lines[i].noperands < 2 ||
            !streq(lines[i].operands[0], "x0") ||
            !has_imm_prefix(lines[i].operands[1])) {
            continue;
        }
        /* mov x1, #B */
        if (!streq(lines[i + 1].mnemonic, "mov") ||
            lines[i + 1].noperands < 2 ||
            !streq(lines[i + 1].operands[0], "x1") ||
            !has_imm_prefix(lines[i + 1].operands[1])) {
            continue;
        }
        /* add/sub/mul x0, x0, x1 */
        if (lines[i + 2].noperands < 3 ||
            !streq(lines[i + 2].operands[0], "x0") ||
            !streq(lines[i + 2].operands[1], "x0") ||
            !streq(lines[i + 2].operands[2], "x1")) {
            continue;
        }

        a = parse_imm(lines[i].operands[1]);
        b = parse_imm(lines[i + 1].operands[1]);

        if (streq(lines[i + 2].mnemonic, "add")) {
            result = a + b;
        } else if (streq(lines[i + 2].mnemonic, "sub")) {
            result = a - b;
        } else if (streq(lines[i + 2].mnemonic, "mul")) {
            result = a * b;
        } else {
            continue;
        }

        /* only fold if result fits in a simple mov imm */
        if (result < -65536 || result > 65535) {
            continue;
        }

        lines[i].removed = 1;
        lines[i + 1].removed = 1;
        sprintf(lines[i + 2].text, "\tmov x0, #%ld", result);
        parse_line(&lines[i + 2]);
        changed = 1;
    }
    return changed;
}

/*
 * Pattern 8: Strength reduction (mul by power of 2 -> shift).
 *   mov x1, #P              where P is a power of 2
 *   mul x0, x0, x1          => lsl x0, x0, #N
 *
 * Also handles: mov x2, #P / mul x1, x1, x2
 */
static int opt_strength_reduce(struct asm_line *lines, int n)
{
    int changed;
    int i;
    long val;
    int shift;

    changed = 0;
    for (i = 0; i < n - 1; i++) {
        if (lines[i].removed || lines[i + 1].removed) {
            continue;
        }
        /* mov xR, #P */
        if (!streq(lines[i].mnemonic, "mov") ||
            lines[i].noperands < 2 ||
            !has_imm_prefix(lines[i].operands[1])) {
            continue;
        }
        val = parse_imm(lines[i].operands[1]);
        if (!is_power_of_two(val, &shift)) {
            continue;
        }
        /* mul xA, xA, xR */
        if (!streq(lines[i + 1].mnemonic, "mul") ||
            lines[i + 1].noperands < 3 ||
            !streq(lines[i + 1].operands[0],
                   lines[i + 1].operands[1]) ||
            !streq(lines[i + 1].operands[2],
                   lines[i].operands[0])) {
            continue;
        }

        lines[i].removed = 1;
        sprintf(lines[i + 1].text, "\tlsl %s, %s, #%d",
                lines[i + 1].operands[0],
                lines[i + 1].operands[1],
                shift);
        parse_line(&lines[i + 1]);
        changed = 1;
    }
    return changed;
}

/*
 * Pattern 9: Redundant comparison.
 *   cmp xA, xB    (or subs xzr, xA, xB)
 *   cmp xA, xB    — remove the second one
 */
static int opt_redundant_cmp(struct asm_line *lines, int n)
{
    int changed;
    int i;

    changed = 0;
    for (i = 0; i < n - 1; i++) {
        if (lines[i].removed || lines[i + 1].removed) {
            continue;
        }
        if (streq(lines[i].mnemonic, "cmp") &&
            streq(lines[i + 1].mnemonic, "cmp") &&
            lines[i].noperands >= 2 &&
            lines[i + 1].noperands >= 2 &&
            streq(lines[i].operands[0], lines[i + 1].operands[0]) &&
            streq(lines[i].operands[1], lines[i + 1].operands[1])) {
            lines[i + 1].removed = 1;
            changed = 1;
        }
        /* subs xzr variant */
        if (streq(lines[i].mnemonic, "subs") &&
            streq(lines[i + 1].mnemonic, "subs") &&
            lines[i].noperands >= 3 &&
            lines[i + 1].noperands >= 3 &&
            streq(lines[i].operands[0], "xzr") &&
            streq(lines[i + 1].operands[0], "xzr") &&
            streq(lines[i].operands[1], lines[i + 1].operands[1]) &&
            streq(lines[i].operands[2], lines[i + 1].operands[2])) {
            lines[i + 1].removed = 1;
            changed = 1;
        }
    }
    return changed;
}

/*
 * Pattern 10: Store-load with different destination register.
 *   str xA, [x29, #-N]
 *   ldr xB, [x29, #-N]     — replace ldr with mov xB, xA
 *
 * Only applies to immediately adjacent instructions.
 */
static int opt_store_load_diff_reg(struct asm_line *lines, int n)
{
    int changed;
    int i;

    changed = 0;
    for (i = 0; i < n - 1; i++) {
        if (lines[i].removed || lines[i + 1].removed) {
            continue;
        }
        /* match str followed by ldr at same address, different reg */
        if (streq(lines[i].mnemonic, "str") &&
            streq(lines[i + 1].mnemonic, "ldr") &&
            lines[i].noperands >= 2 &&
            lines[i + 1].noperands >= 2 &&
            !streq(lines[i].operands[0], lines[i + 1].operands[0]) &&
            streq(lines[i].operands[1], lines[i + 1].operands[1])) {
            /* skip when register widths differ (e.g. str w0 / ldr x0)
             * because mov between w and x registers is invalid */
            if (lines[i].operands[0][0] != lines[i + 1].operands[0][0]) {
                continue;
            }
            if (is_q_reg(lines[i].operands[0]) ||
                is_q_reg(lines[i + 1].operands[0])) {
                continue;
            }
            /* replace ldr xB, [addr] with mov xB, xA
             * (value is still in xA from the preceding str) */
            sprintf(lines[i + 1].text, "\tmov %s, %s",
                    lines[i + 1].operands[0],
                    lines[i].operands[0]);
            parse_line(&lines[i + 1]);
            changed = 1;
        }
    }
    return changed;
}

/*
 * Pattern 10b: Store-load forwarding across safe instructions.
 *   str xR, [x29, #-N]
 *   <safe instructions>       (no clobber of xR or [x29, #-N])
 *   ldr xR, [x29, #-N]       — remove the ldr
 *
 * Only forwards when intervening instructions are provably safe:
 * they must not write to xR, store to the same address, or branch.
 */
static int opt_store_load_forward(struct asm_line *lines, int n)
{
    int changed;
    int i;
    int j;
    int safe;
    const char *reg;
    const char *addr;

    changed = 0;
    for (i = 0; i < n; i++) {
        if (lines[i].removed) {
            continue;
        }
        /* match str xR, [x29, #-N] (not pre/post-index sp ops) */
        if (!streq(lines[i].mnemonic, "str") ||
            lines[i].noperands < 2) {
            continue;
        }
        addr = lines[i].operands[1];
        if (addr[0] != '[' || addr[1] != 'x' ||
            addr[2] != '2' || addr[3] != '9') {
            continue;
        }
        reg = lines[i].operands[0];

        /* scan forward for matching ldr */
        safe = 1;
        for (j = i + 1; j < n && safe; j++) {
            if (lines[j].removed || lines[j].is_comment) {
                continue;
            }
            if (lines[j].is_label) {
                safe = 0;
                break;
            }
            if (lines[j].is_directive) {
                continue;
            }
            /* found matching ldr? */
            if (streq(lines[j].mnemonic, "ldr") &&
                lines[j].noperands >= 2 &&
                streq(lines[j].operands[0], reg) &&
                streq(lines[j].operands[1], addr)) {
                lines[j].removed = 1;
                changed = 1;
                break;
            }
            /* check if this instruction clobbers reg */
            if (lines[j].noperands >= 1 &&
                streq(lines[j].operands[0], reg)) {
                safe = 0;
                break;
            }
            /* check if this instruction stores to same address */
            if ((streq(lines[j].mnemonic, "str") ||
                 streq(lines[j].mnemonic, "strb") ||
                 streq(lines[j].mnemonic, "strh")) &&
                lines[j].noperands >= 2 &&
                streq(lines[j].operands[1], addr)) {
                safe = 0;
                break;
            }
            /* branch/call/ret: give up */
            if (streq(lines[j].mnemonic, "bl") ||
                streq(lines[j].mnemonic, "ret")) {
                safe = 0;
                break;
            }
            /* unconditional branch: give up */
            if (streq(lines[j].mnemonic, "b") &&
                lines[j].noperands >= 1) {
                safe = 0;
                break;
            }
        }
    }
    return changed;
}

/*
 * Pattern 11: Multiply by 1 elimination.
 *   mov xR, #1
 *   mul xA, xA, xR          — remove both (identity operation)
 */
static int opt_mul_one(struct asm_line *lines, int n)
{
    int changed;
    int i;

    changed = 0;
    for (i = 0; i < n - 1; i++) {
        if (lines[i].removed || lines[i + 1].removed) {
            continue;
        }
        /* mov xR, #1 */
        if (!streq(lines[i].mnemonic, "mov") ||
            lines[i].noperands < 2 ||
            !has_imm_prefix(lines[i].operands[1]) ||
            parse_imm(lines[i].operands[1]) != 1) {
            continue;
        }
        /* mul xA, xA, xR */
        if (!streq(lines[i + 1].mnemonic, "mul") ||
            lines[i + 1].noperands < 3 ||
            !streq(lines[i + 1].operands[0],
                   lines[i + 1].operands[1]) ||
            !streq(lines[i + 1].operands[2],
                   lines[i].operands[0])) {
            continue;
        }
        lines[i].removed = 1;
        lines[i + 1].removed = 1;
        changed = 1;
    }
    return changed;
}

/*
 * Pattern 12: Dead code after unconditional branch or ret.
 *   b .Lxx  (or ret)
 *   <instructions>          — remove until next label
 */
static int opt_dead_code(struct asm_line *lines, int n)
{
    int changed;
    int i;
    int j;

    changed = 0;
    for (i = 0; i < n - 1; i++) {
        if (lines[i].removed) {
            continue;
        }
        /* unconditional branch (not conditional b.xx) or ret */
        if (streq(lines[i].mnemonic, "ret") ||
            (streq(lines[i].mnemonic, "b") && lines[i].noperands >= 1)) {
            /* remove subsequent non-label, non-directive lines */
            for (j = i + 1; j < n; j++) {
                if (lines[j].removed) {
                    continue;
                }
                if (lines[j].is_label || lines[j].is_directive) {
                    break;
                }
                if (lines[j].is_comment) {
                    continue;
                }
                lines[j].removed = 1;
                changed = 1;
            }
        }
    }
    return changed;
}

/*
 * Pattern 13: Double branch elimination.
 *   b .L1
 *   b .L2              — remove second (unreachable)
 *
 * Handled by opt_dead_code above, but keeping as explicit pattern
 * is not needed since opt_dead_code covers this case.
 */

/* ---- main optimizer ---- */

/*
 * opt_peephole - optimize assembly text in-place.
 * Reads the text as lines, applies peephole patterns in multiple
 * passes until convergence, then writes surviving lines back.
 * Returns the new length of the text.
 */
int opt_peephole(char *asm_text, int len)
{
    struct asm_line *lines;
    int nlines;
    int max_lines;
    int pass;
    int changed;
    int i;
    char *p;
    char *end;
    char *lstart;
    int llen;
    int out_pos;

    /* count lines first to allocate the right amount */
    max_lines = 0;
    p = asm_text;
    end = asm_text + len;
    while (p < end) {
        while (p < end && *p != '\n') {
            p++;
        }
        if (p < end) {
            p++;
        }
        max_lines++;
    }

    lines = (struct asm_line *)malloc((size_t)max_lines *
                                      sizeof(struct asm_line));
    if (lines == NULL) {
        return len;
    }

    /* split text into lines */
    nlines = 0;
    p = asm_text;
    end = asm_text + len;
    while (p < end && nlines < max_lines) {
        lstart = p;
        while (p < end && *p != '\n') {
            p++;
        }
        llen = (int)(p - lstart);
        if (p < end) {
            p++; /* skip newline */
        }
        lines[nlines].long_text = NULL;
        lines[nlines].long_len = 0;
        if (llen >= MAX_LINE_LEN) {
            /* oversized line: preserve original, mark as directive
             * so optimizer skips it */
            lines[nlines].long_text = lstart;
            lines[nlines].long_len = llen;
            lines[nlines].text[0] = '\0';
            lines[nlines].is_directive = 1;
            lines[nlines].is_label = 0;
            lines[nlines].is_comment = 0;
            lines[nlines].mnemonic[0] = '\0';
            lines[nlines].noperands = 0;
        } else {
            memcpy(lines[nlines].text, lstart, (size_t)llen);
            lines[nlines].text[llen] = '\0';
            strip_trailing(lines[nlines].text);
            parse_line(&lines[nlines]);
        }
        lines[nlines].removed = 0;
        nlines++;
    }

    /* multi-pass optimization */
    for (pass = 0; pass < MAX_PASSES; pass++) {
        changed = 0;
        changed |= opt_push_pop(lines, nlines);
        changed |= opt_store_load(lines, nlines);
        changed |= opt_self_move(lines, nlines);
        changed |= opt_addsub_zero(lines, nlines);
        changed |= opt_dead_mov(lines, nlines);
        changed |= opt_branch_next(lines, nlines);
        changed |= opt_const_fold(lines, nlines);
        changed |= opt_strength_reduce(lines, nlines);
        changed |= opt_redundant_cmp(lines, nlines);
        changed |= opt_store_load_diff_reg(lines, nlines);
        changed |= opt_store_load_forward(lines, nlines);
        changed |= opt_mul_one(lines, nlines);
        changed |= opt_dead_code(lines, nlines);
        if (!changed) {
            break;
        }
    }

    /* reassemble text from surviving lines */
    out_pos = 0;
    for (i = 0; i < nlines; i++) {
        if (lines[i].removed) {
            continue;
        }
        if (lines[i].long_text != NULL) {
            /* oversized line: copy from original source */
            llen = lines[i].long_len;
        } else {
            llen = (int)strlen(lines[i].text);
        }
        if (out_pos + llen + 1 >= len + nlines) {
            break;
        }
        if (lines[i].long_text != NULL) {
            memcpy(asm_text + out_pos, lines[i].long_text,
                   (size_t)llen);
        } else {
            memcpy(asm_text + out_pos, lines[i].text,
                   (size_t)llen);
        }
        out_pos += llen;
        asm_text[out_pos] = '\n';
        out_pos++;
    }
    asm_text[out_pos] = '\0';

    free(lines);
    return out_pos;
}

/* opt_basic_blocks is defined in opt_bb.c */

/* cc_pic_enabled is defined in pic.c, cc_debug_info in dwarf.c,
   gen_x86 in gen_x86.c */
