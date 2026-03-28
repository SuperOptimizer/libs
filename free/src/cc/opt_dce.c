/*
 * opt_dce.c - Dead Code Elimination for the free C compiler.
 * Operates on the SSA IR defined in ir.h.
 *
 * Algorithm:
 *   1. Mark instructions with side effects as live (stores, calls,
 *      returns, branches).
 *   2. Propagate liveness through use-def chains: if a live instruction
 *      uses a value, the defining instruction is also live.
 *   3. Remove all non-live instructions.
 *   4. Clean up empty/unreachable basic blocks.
 *
 * Pure C89. No external dependencies beyond libc.
 */

#include <stdlib.h>
#include <string.h>
#include "ir.h"

#define MAX_LIVE 8192

struct dce_state {
    struct ir_inst *worklist[MAX_LIVE];
    int wl_head, wl_tail;
    /* mark array: indexed by val id, 1 = live */
    int live[MAX_LIVE];
    /* inst-level liveness for void ops (stores, branches, etc.) */
    struct ir_inst *live_insts[MAX_LIVE];
    int nlive_insts;
};

static int is_inst_live(struct dce_state *s, struct ir_inst *inst)
{
    int i;
    /* check if result is marked live */
    if (inst->result && inst->result->id >= 0 &&
        inst->result->id < MAX_LIVE && s->live[inst->result->id])
        return 1;
    /* check void-result instructions */
    for (i = 0; i < s->nlive_insts; i++)
        if (s->live_insts[i] == inst) return 1;
    return 0;
}

static void mark_inst_live(struct dce_state *s, struct ir_inst *inst)
{
    if (!inst) return;

    if (inst->result) {
        if (inst->result->id < 0 || inst->result->id >= MAX_LIVE) return;
        if (s->live[inst->result->id]) return;
        s->live[inst->result->id] = 1;
    } else {
        /* void instruction - check if already in live_insts */
        int i;
        for (i = 0; i < s->nlive_insts; i++)
            if (s->live_insts[i] == inst) return;
        if (s->nlive_insts >= MAX_LIVE) return;
        s->live_insts[s->nlive_insts++] = inst;
    }

    if (s->wl_tail < MAX_LIVE)
        s->worklist[s->wl_tail++] = inst;
}

static int has_side_effects(struct ir_inst *inst)
{
    switch (inst->op) {
    case IR_STORE:
    case IR_CALL:
    case IR_RET:
    case IR_BR:
    case IR_BR_COND:
        return 1;
    default:
        return 0;
    }
}

/* unlink an instruction from its basic block */
static void remove_inst(struct ir_inst *inst)
{
    struct ir_block *bb;
    bb = inst->parent;
    if (!bb) return;
    if (inst->prev) inst->prev->next = inst->next;
    else bb->first = inst->next;
    if (inst->next) inst->next->prev = inst->prev;
    else bb->last = inst->prev;
    inst->prev = NULL;
    inst->next = NULL;
    inst->parent = NULL;
}

void opt_dce(struct ir_func *func)
{
    struct dce_state *s;
    struct ir_block *bb;
    struct ir_inst *inst;
    struct ir_inst *next;
    int i;

    if (!func || !func->blocks) return;

    s = (struct dce_state *)calloc(1, sizeof(struct dce_state));
    if (!s) return;

    /* Step 1: mark side-effecting instructions as live */
    for (bb = func->blocks; bb; bb = bb->next) {
        for (inst = bb->first; inst; inst = inst->next) {
            if (has_side_effects(inst)) {
                mark_inst_live(s, inst);
            }
        }
    }

    /* Step 2: propagate liveness through use-def chains */
    while (s->wl_head < s->wl_tail) {
        inst = s->worklist[s->wl_head++];
        if (!inst) continue;

        /* all operands of a live instruction are also live */
        for (i = 0; i < inst->nargs; i++) {
            if (inst->args[i] && inst->args[i]->def) {
                mark_inst_live(s, inst->args[i]->def);
            }
        }
    }

    /* Step 3: remove dead instructions */
    for (bb = func->blocks; bb; bb = bb->next) {
        for (inst = bb->first; inst; inst = next) {
            next = inst->next;
            if (!is_inst_live(s, inst)) {
                remove_inst(inst);
            }
        }
    }

    free(s);
}
