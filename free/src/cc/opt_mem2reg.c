/*
 * opt_mem2reg.c - Mem2Reg SSA promotion pass for the free C compiler.
 * Operates on the canonical SSA IR defined in ir.h.
 *
 * Promotes stack allocations (alloca) to SSA registers when the alloca
 * is only used by loads and stores.  For single-block cases (most local
 * variables), this is a simple forward scan replacing loads with the
 * last stored value.  For multi-block cases, phi nodes are inserted at
 * iterated dominance frontiers.
 *
 * Pure C89. No external dependencies beyond libc.
 */

#include <stdlib.h>
#include <string.h>
#include "ir.h"

/* ---- limits ---- */
#define MAX_ALLOCAS   256
#define MAX_BLOCKS    1024
#define MAX_PREDS     64
#define RENAME_DEPTH  4096

/* ---- helpers ---- */

/* Check if an alloca is promotable: only used by loads (as args[0])
   and stores (as args[1]) with no other uses. */
static int is_promotable(struct ir_func *f, struct ir_inst *ai)
{
    struct ir_block *bb;
    struct ir_inst *inst;
    int i;

    if (!ai || ai->op != IR_ALLOCA || !ai->result) return 0;

    for (bb = f->blocks; bb; bb = bb->next) {
        for (inst = bb->first; inst; inst = inst->next) {
            if (inst == ai) continue;
            for (i = 0; i < inst->nargs; i++) {
                if (inst->args[i] == ai->result) {
                    /* valid: load from this alloca (arg0 = ptr) */
                    if (inst->op == IR_LOAD && i == 0) continue;
                    /* valid: store to this alloca (arg1 = ptr) */
                    if (inst->op == IR_STORE && i == 1) continue;
                    /* any other use -> not promotable */
                    return 0;
                }
            }
        }
    }
    return 1;
}

/* Unlink an instruction from its basic block */
static void unlink_inst(struct ir_inst *inst)
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

/* Replace all uses of old_val with new_val across the function */
static void replace_all_uses(struct ir_func *f,
                              struct ir_val *old_val,
                              struct ir_val *new_val)
{
    struct ir_block *bb;
    struct ir_inst *inst;
    int i;

    for (bb = f->blocks; bb; bb = bb->next) {
        for (inst = bb->first; inst; inst = inst->next) {
            for (i = 0; i < inst->nargs; i++) {
                if (inst->args[i] == old_val)
                    inst->args[i] = new_val;
            }
        }
    }
}

/* Check if an alloca is only used within a single block */
static int is_single_block(struct ir_func *f, struct ir_inst *ai)
{
    struct ir_block *bb;
    struct ir_inst *inst;
    struct ir_block *use_bb;
    int i;

    use_bb = NULL;
    for (bb = f->blocks; bb; bb = bb->next) {
        for (inst = bb->first; inst; inst = inst->next) {
            if (inst == ai) continue;
            for (i = 0; i < inst->nargs; i++) {
                if (inst->args[i] == ai->result) {
                    if (!use_bb) use_bb = bb;
                    else if (use_bb != bb) return 0;
                    break;
                }
            }
        }
    }
    return 1;
}

/*
 * Single-block promotion: forward scan, track the "current value"
 * of the alloca and replace loads directly.
 */
static void promote_single_block(struct ir_func *f, struct ir_inst *ai)
{
    struct ir_block *bb;
    struct ir_inst *inst;
    struct ir_inst *next;
    struct ir_val *cur_val;
    int i;
    int found;

    /* Find the block containing the uses */
    bb = NULL;
    for (bb = f->blocks; bb; bb = bb->next) {
        found = 0;
        for (inst = bb->first; inst; inst = inst->next) {
            if (inst == ai) continue;
            for (i = 0; i < inst->nargs; i++) {
                if (inst->args[i] == ai->result) {
                    found = 1;
                    break;
                }
            }
            if (found) break;
        }
        if (found) break;
    }

    if (!bb) {
        /* no uses at all, just remove the alloca */
        unlink_inst(ai);
        return;
    }

    cur_val = NULL;

    for (inst = bb->first; inst; inst = next) {
        next = inst->next;

        if (inst->op == IR_STORE && inst->nargs >= 2 &&
            inst->args[1] == ai->result) {
            /* store to this alloca: track the stored value */
            cur_val = inst->args[0];
            unlink_inst(inst);
            continue;
        }

        if (inst->op == IR_LOAD && inst->nargs >= 1 &&
            inst->args[0] == ai->result) {
            /* load from this alloca: replace with current value */
            if (cur_val && inst->result) {
                replace_all_uses(f, inst->result, cur_val);
                unlink_inst(inst);
            }
            continue;
        }
    }

    /* remove the alloca itself */
    unlink_inst(ai);
}

/* ---- Dominator computation (Cooper-Harvey-Kennedy) ---- */

struct m2r_state {
    struct ir_func *func;
    struct ir_block *rpo_order[MAX_BLOCKS];
    int rpo_num[MAX_BLOCKS]; /* block id -> RPO number */
    int nblocks;
    /* block index: block id -> pointer */
    struct ir_block *by_id[MAX_BLOCKS];
};

static void rpo_visit(struct ir_block *b, int *visited,
                       struct m2r_state *s, int *idx)
{
    int i;
    if (!b || b->id < 0 || b->id >= MAX_BLOCKS) return;
    if (visited[b->id]) return;
    visited[b->id] = 1;
    for (i = 0; i < b->nsuccs; i++) {
        if (b->succs && b->succs[i])
            rpo_visit(b->succs[i], visited, s, idx);
    }
    s->rpo_order[*idx] = b;
    s->rpo_num[b->id] = *idx;
    (*idx)--;
}

static void compute_rpo(struct m2r_state *s)
{
    int visited[MAX_BLOCKS];
    int idx;
    memset(visited, 0, sizeof(visited));
    idx = s->nblocks - 1;
    rpo_visit(s->func->entry, visited, s, &idx);
}

static struct ir_block *intersect(struct m2r_state *s,
                                   struct ir_block *a,
                                   struct ir_block *b)
{
    while (a != b) {
        while (a && b && s->rpo_num[a->id] > s->rpo_num[b->id])
            a = a->idom;
        while (a && b && s->rpo_num[b->id] > s->rpo_num[a->id])
            b = b->idom;
    }
    return a;
}

static void compute_dominators(struct m2r_state *s)
{
    int changed;
    int i, j;
    struct ir_block *b;
    struct ir_block *new_idom;
    struct ir_block *p;

    /* init */
    for (b = s->func->blocks; b; b = b->next) b->idom = NULL;
    s->func->entry->idom = s->func->entry;

    changed = 1;
    while (changed) {
        changed = 0;
        for (i = 0; i < s->nblocks; i++) {
            b = s->rpo_order[i];
            if (!b || b == s->func->entry) continue;

            new_idom = NULL;
            for (j = 0; j < b->npreds; j++) {
                p = b->preds[j];
                if (!p || !p->idom) continue;
                if (!new_idom) {
                    new_idom = p;
                } else {
                    new_idom = intersect(s, p, new_idom);
                }
            }
            if (new_idom && b->idom != new_idom) {
                b->idom = new_idom;
                changed = 1;
            }
        }
    }
}

/* ---- Dominance frontiers ---- */

static void compute_dom_frontiers(struct m2r_state *s)
{
    struct ir_block *b;
    int j, k;
    int found;
    struct ir_block *runner;

    /* clear existing frontiers */
    for (b = s->func->blocks; b; b = b->next) {
        b->ndom_frontier = 0;
        b->dom_frontier = NULL;
    }

    for (b = s->func->blocks; b; b = b->next) {
        if (b->npreds < 2) continue;
        for (j = 0; j < b->npreds; j++) {
            runner = b->preds[j];
            while (runner && runner != b->idom) {
                /* add b to runner's dominance frontier */
                found = 0;
                for (k = 0; k < runner->ndom_frontier; k++) {
                    if (runner->dom_frontier[k] == b) {
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    if (!runner->dom_frontier) {
                        runner->dom_frontier = (struct ir_block **)calloc(
                            MAX_PREDS, sizeof(struct ir_block *));
                    }
                    if (runner->ndom_frontier < MAX_PREDS) {
                        runner->dom_frontier[runner->ndom_frontier++] = b;
                    }
                }
                runner = runner->idom;
            }
        }
    }
}

/* ---- Phi insertion and renaming for multi-block allocas ---- */

struct rename_stack {
    struct ir_val *data[RENAME_DEPTH];
    int top;
};

static void stk_push(struct rename_stack *st, struct ir_val *v)
{
    if (st->top < RENAME_DEPTH) st->data[st->top++] = v;
}

static struct ir_val *stk_peek(struct rename_stack *st)
{
    return st->top > 0 ? st->data[st->top - 1] : NULL;
}

/* Insert phi at front of block. Uses arena alloc if available,
   else malloc. */
static struct ir_inst *insert_phi(struct ir_func *f, struct ir_block *bb,
                                   enum ir_type_kind type)
{
    struct ir_inst *phi;
    struct ir_val *v;

    phi = (struct ir_inst *)calloc(1, sizeof(struct ir_inst));
    if (!phi) return NULL;
    phi->op = IR_PHI;

    v = (struct ir_val *)calloc(1, sizeof(struct ir_val));
    if (!v) { free(phi); return NULL; }
    v->id = f->next_val_id++;
    v->type = type;
    v->def = phi;
    phi->result = v;

    phi->parent = bb;
    phi->next = bb->first;
    if (bb->first) bb->first->prev = phi;
    else bb->last = phi;
    bb->first = phi;

    return phi;
}

/* Add a phi argument: (value, from_block) */
static void phi_add_arg(struct ir_inst *phi, struct ir_val *val,
                         struct ir_block *from)
{
    int n;
    struct ir_val **new_args;
    struct ir_block **new_bbs;

    n = phi->nargs + 1;
    new_args = (struct ir_val **)calloc((size_t)n, sizeof(struct ir_val *));
    new_bbs = (struct ir_block **)calloc((size_t)n, sizeof(struct ir_block *));
    if (!new_args || !new_bbs) return;

    if (phi->nargs > 0) {
        memcpy(new_args, phi->args, (size_t)phi->nargs * sizeof(struct ir_val *));
        if (phi->phi_blocks)
            memcpy(new_bbs, phi->phi_blocks,
                   (size_t)phi->nargs * sizeof(struct ir_block *));
    }
    /* Note: we leak old arrays. Acceptable for this pass. */
    new_args[phi->nargs] = val;
    new_bbs[phi->nargs] = from;
    phi->args = new_args;
    phi->phi_blocks = new_bbs;
    phi->nargs = n;
}

/* Determine the type of values stored to this alloca */
static enum ir_type_kind alloca_stored_type(struct ir_func *f,
                                             struct ir_inst *ai)
{
    struct ir_block *bb;
    struct ir_inst *inst;
    for (bb = f->blocks; bb; bb = bb->next) {
        for (inst = bb->first; inst; inst = inst->next) {
            if (inst->op == IR_STORE && inst->nargs >= 2 &&
                inst->args[1] == ai->result && inst->args[0]) {
                return inst->args[0]->type;
            }
        }
    }
    return IRT_I64;
}

/* Collect blocks that contain a store to this alloca */
static int collect_def_blocks(struct ir_func *f, struct ir_inst *ai,
                               struct ir_block **defs, int max_defs)
{
    struct ir_block *bb;
    struct ir_inst *inst;
    int nd;

    nd = 0;
    for (bb = f->blocks; bb; bb = bb->next) {
        for (inst = bb->first; inst; inst = inst->next) {
            if (inst->op == IR_STORE && inst->nargs >= 2 &&
                inst->args[1] == ai->result) {
                if (nd < max_defs) defs[nd++] = bb;
                break; /* one per block is enough */
            }
        }
    }
    return nd;
}

/* Insert phis at iterated dominance frontiers */
static void insert_phis_for(struct ir_func *f, struct ir_inst *ai,
                             enum ir_type_kind type)
{
    struct ir_block *defs[MAX_BLOCKS];
    struct ir_block *wl[MAX_BLOCKS];
    int has_phi[MAX_BLOCKS];
    int in_wl[MAX_BLOCKS];
    int wh, wt, nd;
    int j;
    struct ir_block *b;
    struct ir_block *df_b;

    memset(has_phi, 0, sizeof(has_phi));
    memset(in_wl, 0, sizeof(in_wl));

    nd = collect_def_blocks(f, ai, defs, MAX_BLOCKS);

    wh = wt = 0;
    for (j = 0; j < nd; j++) {
        wl[wt++] = defs[j];
        in_wl[defs[j]->id] = 1;
    }

    while (wh < wt) {
        b = wl[wh++];
        for (j = 0; j < b->ndom_frontier; j++) {
            df_b = b->dom_frontier[j];
            if (!df_b || df_b->id < 0 || df_b->id >= MAX_BLOCKS) continue;
            if (has_phi[df_b->id]) continue;
            has_phi[df_b->id] = 1;
            insert_phi(f, df_b, type);
            if (!in_wl[df_b->id] && wt < MAX_BLOCKS) {
                in_wl[df_b->id] = 1;
                wl[wt++] = df_b;
            }
        }
    }
}

/* Collect dominator tree children into a temporary array */
static int dom_children(struct m2r_state *s, struct ir_block *b,
                         struct ir_block **children, int max_ch)
{
    struct ir_block *c;
    int n;
    n = 0;
    for (c = s->func->blocks; c; c = c->next) {
        if (c->idom == b && c != b) {
            if (n < max_ch) children[n++] = c;
        }
    }
    return n;
}

/* Recursive rename walk */
static void rename_block(struct m2r_state *s, struct ir_block *b,
                          struct ir_inst *ai, struct rename_stack *stk)
{
    struct ir_inst *inst;
    struct ir_inst *next;
    struct ir_val *cur;
    struct ir_block *children[MAX_PREDS];
    int nch;
    int saved;
    int i;

    saved = stk->top;

    for (inst = b->first; inst; inst = next) {
        next = inst->next;

        /* PHI at the top of a block defines a new reaching value */
        if (inst->op == IR_PHI && inst->result) {
            /* We mark phis inserted for this alloca by checking if
               they have no args yet (fresh from insert_phis_for) */
            /* Actually we need a way to identify "our" phis.
               We use a trick: phis we inserted have 0 args initially. */
            if (inst->nargs == 0) {
                stk_push(stk, inst->result);
            }
            continue;
        }

        if (inst->op == IR_STORE && inst->nargs >= 2 &&
            inst->args[1] == ai->result) {
            stk_push(stk, inst->args[0]);
            unlink_inst(inst);
            continue;
        }

        if (inst->op == IR_LOAD && inst->nargs >= 1 &&
            inst->args[0] == ai->result) {
            cur = stk_peek(stk);
            if (cur && inst->result) {
                replace_all_uses(s->func, inst->result, cur);
            }
            unlink_inst(inst);
            continue;
        }
    }

    /* Fill phi arguments in successor blocks */
    for (i = 0; i < b->nsuccs && b->succs; i++) {
        struct ir_block *succ;
        succ = b->succs[i];
        if (!succ) continue;
        for (inst = succ->first; inst; inst = inst->next) {
            if (inst->op != IR_PHI) break;
            /* only fill phis that belong to this alloca:
               they were inserted with 0 args and might now have
               fewer args than the number of preds */
            if (inst->nargs < succ->npreds) {
                cur = stk_peek(stk);
                phi_add_arg(inst, cur, b);
            }
        }
    }

    /* recurse into dominator tree children */
    nch = dom_children(s, b, children, MAX_PREDS);
    for (i = 0; i < nch; i++) {
        rename_block(s, children[i], ai, stk);
    }

    stk->top = saved;
}

/* Remove trivial phis: all operands are the same or self */
static int remove_trivial_phis(struct ir_func *f)
{
    struct ir_block *bb;
    struct ir_inst *inst;
    struct ir_inst *next;
    struct ir_val *same;
    int changed;
    int k;

    changed = 0;
    for (bb = f->blocks; bb; bb = bb->next) {
        for (inst = bb->first; inst; inst = next) {
            next = inst->next;
            if (inst->op != IR_PHI || !inst->result) continue;
            if (inst->nargs == 0) {
                unlink_inst(inst);
                changed = 1;
                continue;
            }
            same = NULL;
            for (k = 0; k < inst->nargs; k++) {
                struct ir_val *v;
                v = inst->args[k];
                if (v == inst->result || !v) continue;
                if (!same) {
                    same = v;
                } else if (same != v) {
                    same = NULL;
                    break;
                }
            }
            if (k < inst->nargs) continue; /* non-trivial */
            if (!same) {
                unlink_inst(inst);
                changed = 1;
                continue;
            }
            replace_all_uses(f, inst->result, same);
            unlink_inst(inst);
            changed = 1;
        }
    }
    return changed;
}

/* Free dom_frontier arrays allocated with calloc */
static void free_dom_frontiers(struct ir_func *f)
{
    struct ir_block *bb;
    for (bb = f->blocks; bb; bb = bb->next) {
        if (bb->dom_frontier) {
            free(bb->dom_frontier);
            bb->dom_frontier = NULL;
        }
        bb->ndom_frontier = 0;
    }
}

/* ---- main entry point ---- */

void opt_mem2reg(struct ir_func *f)
{
    struct ir_inst *allocas[MAX_ALLOCAS];
    struct ir_inst *inst;
    struct m2r_state state;
    struct rename_stack stk;
    enum ir_type_kind type;
    int na;
    int i;

    if (!f || !f->blocks || !f->entry) return;

    /* collect promotable allocas from entry block */
    na = 0;
    for (inst = f->entry->first; inst; inst = inst->next) {
        if (inst->op == IR_ALLOCA && is_promotable(f, inst)) {
            if (na < MAX_ALLOCAS) allocas[na++] = inst;
        }
    }
    if (na == 0) return;

    /* Try single-block promotion first (fast path) */
    for (i = 0; i < na; i++) {
        if (is_single_block(f, allocas[i])) {
            promote_single_block(f, allocas[i]);
            allocas[i] = NULL; /* mark as done */
        }
    }

    /* Check if any multi-block allocas remain */
    {
        int have_multi;
        have_multi = 0;
        for (i = 0; i < na; i++) {
            if (allocas[i]) { have_multi = 1; break; }
        }
        if (!have_multi) return;
    }

    /* Multi-block case: need dominators + phi insertion */
    memset(&state, 0, sizeof(state));
    state.func = f;

    /* count blocks */
    {
        struct ir_block *bb;
        state.nblocks = 0;
        for (bb = f->blocks; bb; bb = bb->next) {
            if (bb->id >= 0 && bb->id < MAX_BLOCKS)
                state.by_id[bb->id] = bb;
            state.nblocks++;
        }
    }

    compute_rpo(&state);
    compute_dominators(&state);
    compute_dom_frontiers(&state);

    for (i = 0; i < na; i++) {
        if (!allocas[i]) continue; /* already promoted */
        type = alloca_stored_type(f, allocas[i]);
        insert_phis_for(f, allocas[i], type);
        memset(&stk, 0, sizeof(stk));
        rename_block(&state, f->entry, allocas[i], &stk);
        unlink_inst(allocas[i]);
    }

    while (remove_trivial_phis(f)) {
        /* keep simplifying */
    }

    free_dom_frontiers(f);
}
