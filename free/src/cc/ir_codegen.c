/*
 * ir_codegen.c - Emit aarch64 assembly from SSA IR.
 * Walks basic blocks in order, translates each IR instruction to
 * one or more aarch64 instructions.  Uses a simple greedy register
 * allocator to map SSA values to physical registers.
 * Pure C89.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "free.h"
#include "ir.h"

/* ---- limits ---- */
#define MAX_VALS      4096
#define MAX_CALLEE    10

/* ---- physical register assignment ---- */
/* caller-saved temporaries we allocate from first: x9-x15 */
/* then callee-saved: x19-x28 */
/* x0-x7 used for args/return, x8 scratch, x16-x17 IP scratch */

#define PHYS_NONE (-1)

/* allocation order: prefer caller-saved, then callee-saved */
static const int alloc_order[] = {
    9, 10, 11, 12, 13, 14, 15,     /* caller-saved temps */
    19, 20, 21, 22, 23, 24, 25, 26, 27, 28  /* callee-saved */
};
#define NUM_ALLOC_REGS 17

/* ---- value-to-register mapping ---- */
struct val_map {
    int val_id;     /* SSA value %N */
    int phys_reg;   /* physical register or PHYS_NONE */
    int spill_off;  /* stack offset if spilled, -1 if not */
    int last_use;   /* instruction index of last use */
};

struct cg_state {
    FILE *out;
    struct ir_func *func;
    struct val_map vals[MAX_VALS];
    int nvals;
    int reg_owner[32];    /* reg_owner[r] = val_id or -1 */
    int callee_used[32];  /* 1 if callee-saved reg was used */
    int next_spill;       /* next spill slot offset (grows down from FP) */
    int inst_idx;         /* current instruction index */
    int label_base;       /* base label number for this function */
};

static int name_eq(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return a == b;
    }
    return strcmp(a, b) == 0;
}

static const char *xn(int r)
{
    static const char *t[] = {
        "x0","x1","x2","x3","x4","x5","x6","x7",
        "x8","x9","x10","x11","x12","x13","x14","x15",
        "x16","x17","x18","x19","x20","x21","x22","x23",
        "x24","x25","x26","x27","x28","x29","x30","sp"
    };
    return (r >= 0 && r <= 31) ? t[r] : "??";
}

static int align16(int v) { return (v + 15) & ~15; }

/* ---- value map operations ---- */

static struct val_map *find_val(struct cg_state *s, int val_id)
{
    int i;
    for (i = 0; i < s->nvals; i++)
        if (s->vals[i].val_id == val_id)
            return &s->vals[i];
    return NULL;
}

static struct val_map *add_val(struct cg_state *s, int val_id)
{
    struct val_map *v;
    if (s->nvals >= MAX_VALS) {
        fprintf(stderr, "ir_codegen: too many values\n");
        exit(1);
    }
    v = &s->vals[s->nvals++];
    v->val_id = val_id;
    v->phys_reg = PHYS_NONE;
    v->spill_off = -1;
    v->last_use = -1;
    return v;
}

/* ---- register allocator ---- */

static void evict_reg(struct cg_state *s, int r)
{
    struct val_map *v;
    int vid;
    vid = s->reg_owner[r];
    if (vid < 0) return;
    v = find_val(s, vid);
    if (v) {
        if (v->spill_off < 0) {
            s->next_spill += 8;
            v->spill_off = s->next_spill;
        }
        fprintf(s->out, "\tstr %s, [x29, #-%d]\n", xn(r), v->spill_off);
        v->phys_reg = PHYS_NONE;
    }
    s->reg_owner[r] = -1;
}

static int alloc_reg(struct cg_state *s, int val_id)
{
    int i, r;

    /* try to find a free register */
    for (i = 0; i < NUM_ALLOC_REGS; i++) {
        r = alloc_order[i];
        if (s->reg_owner[r] < 0) {
            s->reg_owner[r] = val_id;
            if (r >= 19 && r <= 28) s->callee_used[r] = 1;
            return r;
        }
    }
    /* spill the first caller-saved reg */
    r = alloc_order[0];
    evict_reg(s, r);
    s->reg_owner[r] = val_id;
    return r;
}

/* ensure val is in a register; return the phys reg */
static int load_val(struct cg_state *s, int val_id, int scratch)
{
    struct val_map *v;

    if (val_id < 0) return scratch;
    v = find_val(s, val_id);
    if (!v) return scratch;
    if (v->phys_reg != PHYS_NONE) return v->phys_reg;
    if (v->spill_off > 0) {
        fprintf(s->out, "\tldr %s, [x29, #-%d]\n",
                xn(scratch), v->spill_off);
    }
    return scratch;
}

/* assign a register to a newly defined value */
static int def_val(struct cg_state *s, struct ir_val *val)
{
    struct val_map *v;
    int r;

    if (!val) return 9; /* scratch */
    v = find_val(s, val->id);
    if (!v) v = add_val(s, val->id);
    r = alloc_reg(s, val->id);
    v->phys_reg = r;
    return r;
}

/* ---- compute last use for each value ---- */

static void compute_last_uses(struct cg_state *s)
{
    struct ir_block *bb;
    struct ir_inst *inst;
    int idx, i;
    struct val_map *v;

    idx = 0;
    for (bb = s->func->blocks; bb; bb = bb->next) {
        for (inst = bb->first; inst; inst = inst->next) {
            for (i = 0; i < inst->nargs; i++) {
                if (inst->args[i]) {
                    v = find_val(s, inst->args[i]->id);
                    if (!v) v = add_val(s, inst->args[i]->id);
                    v->last_use = idx;
                }
            }
            if (inst->result) {
                v = find_val(s, inst->result->id);
                if (!v) v = add_val(s, inst->result->id);
                if (v->last_use < idx) v->last_use = idx;
            }
            idx++;
        }
    }
}

/* ---- emit helpers ---- */

static void emit_block_label(struct cg_state *s, struct ir_block *bb)
{
    fprintf(s->out, ".LBB%d_%d:\n", s->label_base, bb->id);
}

static void fmt_block_label(struct cg_state *s, struct ir_block *bb,
                             char *buf)
{
    sprintf(buf, ".LBB%d_%d", s->label_base, bb->id);
}

static int func_is_referenced(struct ir_module *mod, const char *name)
{
    struct ir_func *fn;
    struct ir_block *bb;
    struct ir_inst *inst;

    if (name == NULL) {
        return 0;
    }

    for (fn = mod->funcs; fn != NULL; fn = fn->next) {
        for (bb = fn->blocks; bb != NULL; bb = bb->next) {
            for (inst = bb->first; inst != NULL; inst = inst->next) {
                if ((inst->op == IR_CALL || inst->op == IR_GLOBAL_ADDR) &&
                    inst->name != NULL && name_eq(inst->name, name)) {
                    return 1;
                }
            }
        }
    }

    return 0;
}

static void prune_dead_static_funcs(struct ir_module *mod)
{
    struct ir_func *fn;
    struct ir_func *prev;
    struct ir_func *next;
    int changed;

    if (mod == NULL) {
        return;
    }

    do {
        changed = 0;
        prev = NULL;
        for (fn = mod->funcs; fn != NULL; fn = next) {
            next = fn->next;
            if (fn->is_static &&
                (fn->attr_flags & (FREE_ATTR_USED |
                                   FREE_ATTR_CONSTRUCTOR |
                                   FREE_ATTR_DESTRUCTOR)) == 0 &&
                fn->name != NULL &&
                !func_is_referenced(mod, fn->name)) {
                if (prev != NULL) {
                    prev->next = next;
                } else {
                    mod->funcs = next;
                }
                changed = 1;
            } else {
                prev = fn;
            }
        }
    } while (changed);
}

/* emit a 64-bit constant into register */
static void emit_mov_imm(struct cg_state *s, int rd, long imm)
{
    if (imm >= 0 && imm <= 65535) {
        fprintf(s->out, "\tmov %s, #%ld\n", xn(rd), imm);
    } else if (imm >= -65536 && imm < 0) {
        fprintf(s->out, "\tmov %s, #%ld\n", xn(rd), imm);
    } else {
        unsigned long uv;
        int sh, first;
        unsigned long ch;
        uv = (unsigned long)imm;
        first = 1;
        for (sh = 0; sh < 64; sh += 16) {
            ch = (uv >> sh) & 0xFFFFUL;
            if (ch || (sh == 0 && first)) {
                fprintf(s->out, "\t%s %s, #0x%lx, lsl #%d\n",
                        first ? "movz" : "movk", xn(rd), ch, sh);
                first = 0;
            }
        }
        if (first) fprintf(s->out, "\tmov %s, #0\n", xn(rd));
    }
}

/* ---- condition code mapping ---- */

static const char *cmp_cond(enum ir_op op)
{
    switch (op) {
    case IR_EQ:  return "eq";
    case IR_NE:  return "ne";
    case IR_LT:  return "lt";
    case IR_LE:  return "le";
    case IR_GT:  return "gt";
    case IR_GE:  return "ge";
    case IR_ULT: return "lo";
    case IR_ULE: return "ls";
    case IR_UGT: return "hi";
    case IR_UGE: return "hs";
    default:     return "eq";
    }
}

/* ---- instruction emission ---- */

static void emit_inst(struct cg_state *s, struct ir_inst *inst)
{
    int d, s0, s1, r;
    char buf[64];

    switch (inst->op) {
    case IR_CONST:
        if (!inst->result) break;
        d = def_val(s, inst->result);
        emit_mov_imm(s, d, inst->imm);
        break;

    case IR_GLOBAL_ADDR:
        if (!inst->result) break;
        d = def_val(s, inst->result);
        fprintf(s->out, "\tadrp %s, %s\n", xn(d), inst->name);
        fprintf(s->out, "\tadd %s, %s, :lo12:%s\n",
                xn(d), xn(d), inst->name);
        break;

    case IR_ALLOCA:
        if (!inst->result) break;
        /* allocas become frame-relative addresses */
        d = def_val(s, inst->result);
        s->next_spill += (int)inst->imm;
        if (s->next_spill % 8)
            s->next_spill = align16(s->next_spill);
        fprintf(s->out, "\tsub %s, x29, #%d\n",
                xn(d), s->next_spill);
        break;

    case IR_ADD: case IR_SUB: case IR_MUL:
    case IR_AND: case IR_OR: case IR_XOR:
    case IR_SHL: case IR_SHR: case IR_SAR:
        if (!inst->result || inst->nargs < 2) break;
        s0 = load_val(s, inst->args[0]->id, 10);
        s1 = load_val(s, inst->args[1]->id, 11);
        d = def_val(s, inst->result);
        {
            const char *mn = "add";
            switch (inst->op) {
            case IR_ADD: mn = "add"; break;
            case IR_SUB: mn = "sub"; break;
            case IR_MUL: mn = "mul"; break;
            case IR_AND: mn = "and"; break;
            case IR_OR:  mn = "orr"; break;
            case IR_XOR: mn = "eor"; break;
            case IR_SHL: mn = "lsl"; break;
            case IR_SHR: mn = "lsr"; break;
            case IR_SAR: mn = "asr"; break;
            default: break;
            }
            fprintf(s->out, "\t%s %s, %s, %s\n",
                    mn, xn(d), xn(s0), xn(s1));
        }
        break;

    case IR_DIV:
        if (!inst->result || inst->nargs < 2) break;
        s0 = load_val(s, inst->args[0]->id, 10);
        s1 = load_val(s, inst->args[1]->id, 11);
        d = def_val(s, inst->result);
        fprintf(s->out, "\tsdiv %s, %s, %s\n",
                xn(d), xn(s0), xn(s1));
        break;

    case IR_MOD:
        if (!inst->result || inst->nargs < 2) break;
        s0 = load_val(s, inst->args[0]->id, 10);
        s1 = load_val(s, inst->args[1]->id, 11);
        d = def_val(s, inst->result);
        /* x8 = a / b; result = a - x8 * b */
        fprintf(s->out, "\tsdiv x8, %s, %s\n", xn(s0), xn(s1));
        fprintf(s->out, "\tmsub %s, x8, %s, %s\n",
                xn(d), xn(s1), xn(s0));
        break;

    case IR_NEG:
        if (!inst->result || inst->nargs < 1) break;
        s0 = load_val(s, inst->args[0]->id, 10);
        d = def_val(s, inst->result);
        fprintf(s->out, "\tneg %s, %s\n", xn(d), xn(s0));
        break;

    case IR_NOT:
        if (!inst->result || inst->nargs < 1) break;
        s0 = load_val(s, inst->args[0]->id, 10);
        d = def_val(s, inst->result);
        fprintf(s->out, "\tmvn %s, %s\n", xn(d), xn(s0));
        break;

    /* comparisons */
    case IR_EQ: case IR_NE: case IR_LT: case IR_LE:
    case IR_GT: case IR_GE: case IR_ULT: case IR_ULE:
    case IR_UGT: case IR_UGE:
        if (!inst->result || inst->nargs < 2) break;
        s0 = load_val(s, inst->args[0]->id, 10);
        s1 = load_val(s, inst->args[1]->id, 11);
        d = def_val(s, inst->result);
        fprintf(s->out, "\tcmp %s, %s\n", xn(s0), xn(s1));
        fprintf(s->out, "\tcset %s, %s\n",
                xn(d), cmp_cond(inst->op));
        break;

    case IR_LOAD:
        if (!inst->result || inst->nargs < 1) break;
        s0 = load_val(s, inst->args[0]->id, 10);
        d = def_val(s, inst->result);
        {
            enum ir_type_kind t;
            t = inst->result->type;
            if (t == IRT_I8) {
                fprintf(s->out, "\tldrsb %s, [%s]\n",
                        xn(d), xn(s0));
            } else if (t == IRT_I16) {
                fprintf(s->out, "\tldrsh %s, [%s]\n",
                        xn(d), xn(s0));
            } else if (t == IRT_I32) {
                fprintf(s->out, "\tldrsw %s, [%s]\n",
                        xn(d), xn(s0));
            } else {
                fprintf(s->out, "\tldr %s, [%s]\n",
                        xn(d), xn(s0));
            }
        }
        break;

    case IR_STORE:
        if (inst->nargs < 2) break;
        s0 = load_val(s, inst->args[0]->id, 10); /* value */
        s1 = load_val(s, inst->args[1]->id, 11); /* address */
        {
            enum ir_type_kind t;
            t = inst->args[0]->type;
            if (t == IRT_I8 || t == IRT_I1) {
                fprintf(s->out, "\tstrb w%d, [%s]\n",
                        s0, xn(s1));
            } else if (t == IRT_I16) {
                fprintf(s->out, "\tstrh w%d, [%s]\n",
                        s0, xn(s1));
            } else if (t == IRT_I32) {
                fprintf(s->out, "\tstr w%d, [%s]\n",
                        s0, xn(s1));
            } else {
                fprintf(s->out, "\tstr %s, [%s]\n",
                        xn(s0), xn(s1));
            }
        }
        break;

    /* conversions */
    case IR_SEXT:
        if (!inst->result || inst->nargs < 1) break;
        s0 = load_val(s, inst->args[0]->id, 10);
        d = def_val(s, inst->result);
        {
            enum ir_type_kind src_t;
            src_t = inst->args[0]->type;
            if (src_t == IRT_I8) {
                fprintf(s->out, "\tsxtb %s, w%d\n", xn(d), s0);
            } else if (src_t == IRT_I16) {
                fprintf(s->out, "\tsxth %s, w%d\n", xn(d), s0);
            } else if (src_t == IRT_I32 || src_t == IRT_I1) {
                fprintf(s->out, "\tsxtw %s, w%d\n", xn(d), s0);
            } else if (s0 != d) {
                fprintf(s->out, "\tmov %s, %s\n", xn(d), xn(s0));
            }
        }
        break;

    case IR_ZEXT:
        if (!inst->result || inst->nargs < 1) break;
        s0 = load_val(s, inst->args[0]->id, 10);
        d = def_val(s, inst->result);
        {
            enum ir_type_kind src_t;
            src_t = inst->args[0]->type;
            if (src_t == IRT_I8) {
                fprintf(s->out, "\tuxtb w%d, w%d\n", d, s0);
            } else if (src_t == IRT_I16) {
                fprintf(s->out, "\tuxth w%d, w%d\n", d, s0);
            } else if (src_t == IRT_I32 || src_t == IRT_I1) {
                /* 32-bit ops zero-extend to 64 on aarch64 */
                fprintf(s->out, "\tmov w%d, w%d\n", d, s0);
            } else if (s0 != d) {
                fprintf(s->out, "\tmov %s, %s\n", xn(d), xn(s0));
            }
        }
        break;

    case IR_TRUNC:
        if (!inst->result || inst->nargs < 1) break;
        s0 = load_val(s, inst->args[0]->id, 10);
        d = def_val(s, inst->result);
        {
            enum ir_type_kind dst_t;
            dst_t = inst->result->type;
            if (dst_t == IRT_I8) {
                fprintf(s->out, "\tand %s, %s, #0xff\n",
                        xn(d), xn(s0));
            } else if (dst_t == IRT_I16) {
                fprintf(s->out, "\tand %s, %s, #0xffff\n",
                        xn(d), xn(s0));
            } else if (dst_t == IRT_I32) {
                fprintf(s->out, "\tmov w%d, w%d\n", d, s0);
            } else if (dst_t == IRT_I1) {
                fprintf(s->out, "\tand %s, %s, #0x1\n",
                        xn(d), xn(s0));
            } else if (s0 != d) {
                fprintf(s->out, "\tmov %s, %s\n", xn(d), xn(s0));
            }
        }
        break;

    case IR_BITCAST: case IR_PTRTOINT: case IR_INTTOPTR:
        if (!inst->result || inst->nargs < 1) break;
        s0 = load_val(s, inst->args[0]->id, 10);
        d = def_val(s, inst->result);
        if (s0 != d)
            fprintf(s->out, "\tmov %s, %s\n", xn(d), xn(s0));
        break;

    /* control flow */
    case IR_BR:
        fmt_block_label(s, inst->target, buf);
        fprintf(s->out, "\tb %s\n", buf);
        break;

    case IR_BR_COND:
        if (inst->nargs < 1) break;
        s0 = load_val(s, inst->args[0]->id, 10);
        fprintf(s->out, "\tcmp %s, #0\n", xn(s0));
        fmt_block_label(s, inst->true_bb, buf);
        fprintf(s->out, "\tb.ne %s\n", buf);
        fmt_block_label(s, inst->false_bb, buf);
        fprintf(s->out, "\tb %s\n", buf);
        break;

    case IR_RET:
        if (inst->nargs > 0 && inst->args[0]) {
            s0 = load_val(s, inst->args[0]->id, 0);
            if (s0 != 0)
                fprintf(s->out, "\tmov x0, %s\n", xn(s0));
        }
        fprintf(s->out, "\tb .Lret_%s\n", s->func->name);
        break;

    case IR_CALL: {
        int a, ap, nca;
        /* save caller-saved regs that are live */
        for (r = 9; r <= 15; r++) {
            if (s->reg_owner[r] >= 0)
                evict_reg(s, r);
        }
        /* also save x0-x7 if they hold live values */
        for (r = 0; r <= 7; r++) {
            if (s->reg_owner[r] >= 0)
                evict_reg(s, r);
        }
        /* move args to x0-x7 */
        nca = inst->nargs;
        if (nca > 8) nca = 8;
        for (a = 0; a < nca; a++) {
            if (inst->args[a]) {
                ap = load_val(s, inst->args[a]->id, a);
                if (ap != a)
                    fprintf(s->out, "\tmov x%d, %s\n",
                            a, xn(ap));
            }
        }
        fprintf(s->out, "\tbl %s\n", inst->name);
        /* result in x0 */
        if (inst->result) {
            d = def_val(s, inst->result);
            if (d != 0)
                fprintf(s->out, "\tmov %s, x0\n", xn(d));
            else
                s->reg_owner[0] = inst->result->id;
        }
        break;
    }

    case IR_PHI:
        /* PHI nodes handled during block transitions; just ensure
         * the result has a register assignment */
        if (inst->result) {
            struct val_map *v;
            v = find_val(s, inst->result->id);
            if (!v) v = add_val(s, inst->result->id);
            if (v->phys_reg == PHYS_NONE && v->spill_off < 0) {
                v->phys_reg = alloc_reg(s, inst->result->id);
            }
        }
        break;

    default:
        break;
    }
    (void)r; /* suppress unused warning in non-CALL paths */
}

/* ---- phi resolution: emit moves for phi sources before branch ---- */

static void resolve_phis(struct cg_state *s, struct ir_block *from,
                          struct ir_block *to)
{
    struct ir_inst *phi;
    int i, src_reg, dst_reg;
    struct val_map *dv;

    for (phi = to->first; phi; phi = phi->next) {
        if (phi->op != IR_PHI) break; /* phis are at top of block */
        if (!phi->result || !phi->phi_blocks) continue;
        for (i = 0; i < phi->nargs; i++) {
            if (phi->phi_blocks[i] == from && phi->args[i]) {
                /* move the source value into the phi's register */
                dv = find_val(s, phi->result->id);
                if (!dv) {
                    dv = add_val(s, phi->result->id);
                    dv->phys_reg = alloc_reg(s, phi->result->id);
                }
                dst_reg = dv->phys_reg;
                if (dst_reg == PHYS_NONE) {
                    dst_reg = alloc_reg(s, phi->result->id);
                    dv->phys_reg = dst_reg;
                }
                src_reg = load_val(s, phi->args[i]->id, 8);
                if (src_reg != dst_reg)
                    fprintf(s->out, "\tmov %s, %s\n",
                            xn(dst_reg), xn(src_reg));
                break;
            }
        }
    }
}

/* ---- two-pass function emission ---- */

static void ir_codegen_func(struct ir_func *f, FILE *out, int label_base)
{
    struct cg_state *s;
    FILE *tmp;
    char *body_buf;
    long body_len;
    size_t nread;
    int i, ncsaved, fsz, off;
    int csaved_regs[MAX_CALLEE];
    struct ir_block *bb;
    struct ir_inst *inst;

    s = (struct cg_state *)calloc(1, sizeof(struct cg_state));
    if (!s) {
        fprintf(stderr, "ir_codegen: out of memory\n");
        exit(1);
    }

    s->func = f;
    s->label_base = label_base;
    memset(s->reg_owner, 0xff, sizeof(s->reg_owner));

    /* pass 1: emit body to temp buffer to discover frame needs */
    tmp = tmpfile();
    if (!tmp) { free(s); return; }
    s->out = tmp;

    /* pre-register parameters */
    for (i = 0; i < f->nparams && i < 8; i++) {
        if (f->params[i]) {
            struct val_map *v;
            v = add_val(s, f->params[i]->id);
            v->phys_reg = i;
            s->reg_owner[i] = f->params[i]->id;
        }
    }

    compute_last_uses(s);

    /* emit body (blocks) */
    for (bb = f->blocks; bb; bb = bb->next) {
        emit_block_label(s, bb);
        for (inst = bb->first; inst; inst = inst->next) {
            if (inst->op == IR_BR) {
                resolve_phis(s, bb, inst->target);
            }
            emit_inst(s, inst);
            s->inst_idx++;
        }
    }

    /* read body back */
    fflush(tmp);
    body_len = ftell(tmp);
    fseek(tmp, 0, SEEK_SET);
    body_buf = (char *)malloc((size_t)body_len + 1);
    if (!body_buf) { fclose(tmp); free(s); return; }
    nread = fread(body_buf, 1, (size_t)body_len, tmp);
    body_buf[nread] = '\0';
    fclose(tmp);

    /* collect callee-saved regs used */
    ncsaved = 0;
    for (i = 19; i <= 28; i++) {
        if (s->callee_used[i])
            csaved_regs[ncsaved++] = i;
    }

    /* compute frame size: 16 (FP/LR) + spills + callee saves */
    fsz = align16(16 + s->next_spill + ncsaved * 8);
    if (fsz < 16) fsz = 16;

    /* pass 2: emit to real output */
    fprintf(out, "\n\t.global %s\n", f->name);
    fprintf(out, "\t.type %s, %%function\n", f->name);
    fprintf(out, "\t.p2align 2\n");
    fprintf(out, "%s:\n", f->name);

    /* prologue */
    fprintf(out, "\tstp x29, x30, [sp, #-%d]!\n", fsz);
    fprintf(out, "\tmov x29, sp\n");

    /* save callee-saved regs */
    off = fsz - 16;
    for (i = 0; i < ncsaved; i++) {
        fprintf(out, "\tstr %s, [x29, #-%d]\n",
                xn(csaved_regs[i]), off);
        off -= 8;
    }

    /* body */
    fwrite(body_buf, 1, nread, out);

    /* epilogue */
    fprintf(out, ".Lret_%s:\n", f->name);

    /* restore callee-saved regs */
    off = fsz - 16;
    for (i = 0; i < ncsaved; i++) {
        fprintf(out, "\tldr %s, [x29, #-%d]\n",
                xn(csaved_regs[i]), off);
        off -= 8;
    }

    fprintf(out, "\tldp x29, x30, [sp], #%d\n", fsz);
    fprintf(out, "\tret\n");
    fprintf(out, "\t.size %s, .-%s\n", f->name, f->name);

    free(body_buf);
    free(s);
}

/* ---- public interface ---- */

/*
 * ir_codegen - emit aarch64 assembly for an entire IR module.
 * Called from cc.c at -O2+ instead of gen().
 */
void ir_codegen(struct ir_module *mod, FILE *out)
{
    struct ir_func *f;
    struct ir_global *g;
    int label_base;

    if (!mod) return;

    fprintf(out, "/* generated by free-cc (IR codegen) */\n");
    fprintf(out, "\t.arch armv8-a\n");

    prune_dead_static_funcs(mod);

    /* emit globals */
    for (g = mod->globals; g; g = g->next) {
        if (g->init_val != 0) {
            fprintf(out, "\n\t.data\n");
            fprintf(out, "\t.global %s\n", g->name);
            fprintf(out, "\t.p2align %d\n",
                    g->align > 1 ?
                    (g->align >= 8 ? 3 :
                     (g->align >= 4 ? 2 : 1)) : 0);
            fprintf(out, "%s:\n", g->name);
            if (g->size <= 4)
                fprintf(out, "\t.word %ld\n", g->init_val);
            else
                fprintf(out, "\t.xword %ld\n", g->init_val);
        } else {
            fprintf(out, "\n\t.bss\n");
            fprintf(out, "\t.global %s\n", g->name);
            fprintf(out, "\t.p2align %d\n",
                    g->align > 1 ?
                    (g->align >= 8 ? 3 :
                     (g->align >= 4 ? 2 : 1)) : 0);
            fprintf(out, "%s:\n", g->name);
            fprintf(out, "\t.zero %d\n", g->size);
        }
    }

    /* emit functions */
    fprintf(out, "\n\t.text\n");
    label_base = 0;
    for (f = mod->funcs; f; f = f->next) {
        ir_codegen_func(f, out, label_base);
        label_base += f->next_block_id + 1;
    }

    /* mark stack as non-executable */
    fprintf(out, "\n\t.section .note.GNU-stack,\"\",%%progbits\n");
}
