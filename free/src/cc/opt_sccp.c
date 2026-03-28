/*
 * opt_sccp.c - Sparse Conditional Constant Propagation for the free C compiler.
 * Operates on the SSA IR defined in ir.h.
 *
 * Algorithm:
 *   - Lattice per SSA value: TOP (unknown) -> CONSTANT(n) -> BOTTOM (variable)
 *   - Two worklists: SSA edges (value changed) and CFG edges (block reachable)
 *   - Evaluate instructions to determine lattice values
 *   - Fold constants and remove dead branches
 *
 * Pure C89. No external dependencies beyond libc.
 */

#include <stdlib.h>
#include <string.h>
#include "ir.h"

/* ---- lattice ---- */

#define LAT_TOP    0   /* not yet determined */
#define LAT_CONST  1   /* known constant */
#define LAT_BOTTOM 2   /* variable (overdefined) */

#define MAX_VALS   4096
#define MAX_BLOCKS 1024
#define MAX_WORKLIST 8192

struct lattice_val {
    int state;   /* LAT_TOP, LAT_CONST, LAT_BOTTOM */
    long cval;   /* constant value when state == LAT_CONST */
};

struct sccp_state {
    struct lattice_val vals[MAX_VALS];
    int block_reachable[MAX_BLOCKS];

    /* SSA worklist: instructions whose output changed */
    struct ir_inst *ssa_wl[MAX_WORKLIST];
    int ssa_head, ssa_tail;

    /* CFG worklist: blocks newly reachable */
    struct ir_block *cfg_wl[MAX_WORKLIST];
    int cfg_head, cfg_tail;

    struct ir_func *func;
};

static struct lattice_val *get_lat(struct sccp_state *s, int val_id)
{
    if (val_id < 0 || val_id >= MAX_VALS) return NULL;
    return &s->vals[val_id];
}

static struct lattice_val val_lat(struct sccp_state *s, struct ir_val *v)
{
    struct lattice_val r;
    struct lattice_val *lv;
    if (!v) { r.state = LAT_BOTTOM; r.cval = 0; return r; }
    lv = get_lat(s, v->id);
    if (lv) return *lv;
    r.state = LAT_TOP; r.cval = 0;
    return r;
}

/* Push to SSA worklist: all instructions that use this value */
static void push_ssa_users(struct sccp_state *s, struct ir_val *v)
{
    struct ir_block *bb;
    struct ir_inst *inst;
    int i;

    if (!v) return;
    for (bb = s->func->blocks; bb; bb = bb->next) {
        for (inst = bb->first; inst; inst = inst->next) {
            for (i = 0; i < inst->nargs; i++) {
                if (inst->args[i] == v) {
                    if (s->ssa_tail < MAX_WORKLIST)
                        s->ssa_wl[s->ssa_tail++] = inst;
                    break;
                }
            }
        }
    }
}

static void push_cfg(struct sccp_state *s, struct ir_block *bb)
{
    if (!bb) return;
    if (bb->id >= 0 && bb->id < MAX_BLOCKS && s->block_reachable[bb->id])
        return; /* already reachable */
    if (s->cfg_tail < MAX_WORKLIST)
        s->cfg_wl[s->cfg_tail++] = bb;
}

/* Return 1 if the lattice was lowered (changed) */
static int lower_lat(struct sccp_state *s, int val_id,
                     int new_state, long new_cval)
{
    struct lattice_val *lv;
    lv = get_lat(s, val_id);
    if (!lv) return 0;

    if (lv->state == LAT_BOTTOM) return 0; /* can't go lower */

    if (lv->state == LAT_TOP) {
        lv->state = new_state;
        lv->cval = new_cval;
        return 1;
    }

    /* lv->state == LAT_CONST */
    if (new_state == LAT_CONST && lv->cval == new_cval) return 0;

    /* conflicting constant or BOTTOM -> go to BOTTOM */
    lv->state = LAT_BOTTOM;
    lv->cval = 0;
    return 1;
}

/* ---- instruction evaluation ---- */

static long eval_binop(enum ir_op op, long a, long b)
{
    switch (op) {
    case IR_ADD: return a + b;
    case IR_SUB: return a - b;
    case IR_MUL: return a * b;
    case IR_DIV: return b != 0 ? a / b : 0;
    case IR_MOD: return b != 0 ? a % b : 0;
    case IR_AND: return a & b;
    case IR_OR:  return a | b;
    case IR_XOR: return a ^ b;
    case IR_SHL: return a << b;
    case IR_SHR: return (long)((unsigned long)a >> b);
    case IR_SAR: return a >> b;
    case IR_EQ:  return a == b ? 1 : 0;
    case IR_NE:  return a != b ? 1 : 0;
    case IR_LT:  return a < b ? 1 : 0;
    case IR_LE:  return a <= b ? 1 : 0;
    case IR_GT:  return a > b ? 1 : 0;
    case IR_GE:  return a >= b ? 1 : 0;
    case IR_ULT: return (unsigned long)a < (unsigned long)b ? 1 : 0;
    case IR_ULE: return (unsigned long)a <= (unsigned long)b ? 1 : 0;
    case IR_UGT: return (unsigned long)a > (unsigned long)b ? 1 : 0;
    case IR_UGE: return (unsigned long)a >= (unsigned long)b ? 1 : 0;
    default: return 0;
    }
}

static long eval_unop(enum ir_op op, long a)
{
    switch (op) {
    case IR_NEG:   return -a;
    case IR_NOT:   return ~a;
    case IR_SEXT:  return a;  /* simplified; real impl needs width */
    case IR_ZEXT:  return a;
    case IR_TRUNC: return a;
    case IR_BITCAST: case IR_PTRTOINT: case IR_INTTOPTR:
        return a;
    default: return 0;
    }
}

static void eval_inst(struct sccp_state *s, struct ir_inst *inst)
{
    struct lattice_val la, lb;
    int new_state;
    long new_cval;

    if (!inst || !inst->result) {
        /* handle terminators */
        if (inst && inst->op == IR_BR) {
            push_cfg(s, inst->target);
        } else if (inst && inst->op == IR_BR_COND) {
            if (inst->nargs >= 1 && inst->args[0]) {
                la = val_lat(s, inst->args[0]);
                if (la.state == LAT_CONST) {
                    /* known direction */
                    if (la.cval != 0)
                        push_cfg(s, inst->true_bb);
                    else
                        push_cfg(s, inst->false_bb);
                } else if (la.state == LAT_BOTTOM) {
                    push_cfg(s, inst->true_bb);
                    push_cfg(s, inst->false_bb);
                }
                /* LAT_TOP: don't push either yet */
            } else {
                push_cfg(s, inst->true_bb);
                push_cfg(s, inst->false_bb);
            }
        }
        return;
    }

    new_state = LAT_TOP;
    new_cval = 0;

    switch (inst->op) {
    case IR_CONST:
        new_state = LAT_CONST;
        new_cval = inst->imm;
        break;

    case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV: case IR_MOD:
    case IR_AND: case IR_OR: case IR_XOR:
    case IR_SHL: case IR_SHR: case IR_SAR:
    case IR_EQ: case IR_NE: case IR_LT: case IR_LE:
    case IR_GT: case IR_GE: case IR_ULT: case IR_ULE:
    case IR_UGT: case IR_UGE:
        if (inst->nargs < 2) { new_state = LAT_BOTTOM; break; }
        la = val_lat(s, inst->args[0]);
        lb = val_lat(s, inst->args[1]);
        if (la.state == LAT_BOTTOM || lb.state == LAT_BOTTOM) {
            new_state = LAT_BOTTOM;
        } else if (la.state == LAT_CONST && lb.state == LAT_CONST) {
            new_state = LAT_CONST;
            new_cval = eval_binop(inst->op, la.cval, lb.cval);
        }
        /* else one is TOP -> stay TOP */
        break;

    case IR_NEG: case IR_NOT:
    case IR_SEXT: case IR_ZEXT: case IR_TRUNC:
    case IR_BITCAST: case IR_PTRTOINT: case IR_INTTOPTR:
        if (inst->nargs < 1) { new_state = LAT_BOTTOM; break; }
        la = val_lat(s, inst->args[0]);
        if (la.state == LAT_BOTTOM) {
            new_state = LAT_BOTTOM;
        } else if (la.state == LAT_CONST) {
            new_state = LAT_CONST;
            new_cval = eval_unop(inst->op, la.cval);
        }
        break;

    case IR_PHI: {
        int i;
        int found_const = 0;
        long phi_cval = 0;
        new_state = LAT_TOP;
        for (i = 0; i < inst->nargs; i++) {
            struct lattice_val pv;
            /* only consider args from reachable predecessors */
            if (inst->phi_blocks && inst->phi_blocks[i]) {
                int bid;
                bid = inst->phi_blocks[i]->id;
                if (bid >= 0 && bid < MAX_BLOCKS &&
                    !s->block_reachable[bid])
                    continue;
            }
            pv = val_lat(s, inst->args[i]);
            if (pv.state == LAT_BOTTOM) {
                new_state = LAT_BOTTOM;
                break;
            }
            if (pv.state == LAT_CONST) {
                if (!found_const) {
                    found_const = 1;
                    phi_cval = pv.cval;
                    new_state = LAT_CONST;
                    new_cval = pv.cval;
                } else if (pv.cval != phi_cval) {
                    new_state = LAT_BOTTOM;
                    break;
                }
            }
            /* LAT_TOP args are ignored (optimistic) */
        }
        break;
    }

    case IR_LOAD:
    case IR_CALL:
    case IR_GLOBAL_ADDR:
    case IR_ALLOCA:
        /* can't constant-fold these */
        new_state = LAT_BOTTOM;
        break;

    default:
        new_state = LAT_BOTTOM;
        break;
    }

    if (lower_lat(s, inst->result->id, new_state, new_cval)) {
        push_ssa_users(s, inst->result);
    }
}

/* ---- main SCCP algorithm ---- */

void opt_sccp(struct ir_func *func, struct arena *arena)
{
    struct sccp_state *s;
    struct ir_block *bb;
    struct ir_inst *inst;
    struct ir_inst *next;

    if (!func || !func->entry) return;

    s = (struct sccp_state *)calloc(1, sizeof(struct sccp_state));
    if (!s) return;
    s->func = func;

    /* seed: entry block is reachable */
    push_cfg(s, func->entry);

    /* iterate until both worklists are empty */
    while (s->cfg_head < s->cfg_tail || s->ssa_head < s->ssa_tail) {
        /* process CFG worklist */
        while (s->cfg_head < s->cfg_tail) {
            bb = s->cfg_wl[s->cfg_head++];
            if (!bb || bb->id < 0 || bb->id >= MAX_BLOCKS) continue;
            if (s->block_reachable[bb->id]) continue;
            s->block_reachable[bb->id] = 1;

            /* evaluate all instructions in the newly reachable block */
            for (inst = bb->first; inst; inst = inst->next) {
                eval_inst(s, inst);
            }
        }

        /* process SSA worklist */
        while (s->ssa_head < s->ssa_tail) {
            inst = s->ssa_wl[s->ssa_head++];
            if (!inst || !inst->parent) continue;
            /* only re-evaluate if the block is reachable */
            if (inst->parent->id >= 0 && inst->parent->id < MAX_BLOCKS &&
                !s->block_reachable[inst->parent->id])
                continue;
            eval_inst(s, inst);
        }
    }

    /* ---- rewrite phase ---- */

    for (bb = func->blocks; bb; bb = bb->next) {
        if (bb->id >= 0 && bb->id < MAX_BLOCKS &&
            !s->block_reachable[bb->id])
            continue;

        for (inst = bb->first; inst; inst = next) {
            next = inst->next;

            /* replace instructions whose result is constant */
            if (inst->result && inst->op != IR_CONST) {
                struct lattice_val *lv;
                lv = get_lat(s, inst->result->id);
                if (lv && lv->state == LAT_CONST) {
                    /* rewrite to IR_CONST */
                    inst->op = IR_CONST;
                    inst->imm = lv->cval;
                    inst->nargs = 0;
                    inst->args = NULL;
                }
            }

            /* fold conditional branches with known condition */
            if (inst->op == IR_BR_COND && inst->nargs >= 1 &&
                inst->args[0]) {
                struct lattice_val *lv;
                lv = get_lat(s, inst->args[0]->id);
                if (lv && lv->state == LAT_CONST) {
                    /* convert to unconditional branch */
                    if (lv->cval != 0) {
                        inst->op = IR_BR;
                        inst->target = inst->true_bb;
                    } else {
                        inst->op = IR_BR;
                        inst->target = inst->false_bb;
                    }
                    inst->nargs = 0;
                    inst->args = NULL;
                    inst->true_bb = NULL;
                    inst->false_bb = NULL;
                }
            }
        }
    }

    (void)arena; /* reserved for future use */
    free(s);
}
