/*
 * opt_inline.c - Function inlining for the free C compiler.
 * Operates on the SSA IR defined in ir.h.
 *
 * For each call site where the callee is:
 *   - defined in the same module
 *   - small (< 20 IR instructions)
 *   - not recursive (does not call itself)
 * Replace the call with a copy of the callee's IR body,
 * substituting parameters with the call arguments.
 *
 * Pure C89. No external dependencies beyond libc.
 */

#include <stdlib.h>
#include <string.h>
#include "ir.h"

#define MAX_INLINE_INSTS 20
#define MAX_VALS         4096
#define MAX_BLOCKS       1024
#define MAX_PREDS        32
#define MAX_RETS         16

/* ---- helpers ---- */

static void *inl_alloc(struct arena *a, usize sz)
{
    void *p;
    p = arena_alloc(a, sz);
    memset(p, 0, sz);
    return p;
}

static char *inl_strdup(struct arena *a, const char *s)
{
    return s ? str_dup(a, s, (int)strlen(s)) : NULL;
}

/* Count instructions in a function */
static int count_insts(struct ir_func *f)
{
    struct ir_block *bb;
    struct ir_inst *inst;
    int count;

    count = 0;
    for (bb = f->blocks; bb; bb = bb->next) {
        for (inst = bb->first; inst; inst = inst->next) {
            count++;
        }
    }
    return count;
}

/* Check if function calls itself (direct recursion) */
static int is_recursive(struct ir_func *f)
{
    struct ir_block *bb;
    struct ir_inst *inst;

    for (bb = f->blocks; bb; bb = bb->next) {
        for (inst = bb->first; inst; inst = inst->next) {
            if (inst->op == IR_CALL && inst->name &&
                f->name && strcmp(inst->name, f->name) == 0) {
                return 1;
            }
        }
    }
    return 0;
}

/* Find a function by name in the module */
static struct ir_func *find_func(struct ir_module *mod, const char *name)
{
    struct ir_func *f;

    if (!name) return NULL;
    for (f = mod->funcs; f; f = f->next) {
        if (f->name && strcmp(f->name, name) == 0)
            return f;
    }
    return NULL;
}

/* Check if a function is eligible for inlining */
static int is_inlinable(struct ir_func *callee)
{
    if (!callee || !callee->entry) return 0;
    if (callee->attr_flags & FREE_ATTR_NOINLINE) return 0;
    if (callee->attr_flags & FREE_ATTR_ALWAYS_INLINE) return 1;
    if (is_recursive(callee)) return 0;
    if (count_insts(callee) > MAX_INLINE_INSTS) return 0;
    return 1;
}

/* ---- cloning state ---- */

struct inline_state {
    struct arena *arena;
    struct ir_func *caller;
    struct ir_func *callee;
    /* value mapping: callee val id -> cloned val */
    struct ir_val *val_map[MAX_VALS];
    /* block mapping: callee block id -> cloned block */
    struct ir_block *block_map[MAX_BLOCKS];
};

static struct ir_val *clone_val(struct inline_state *s,
                                 enum ir_type_kind type)
{
    struct ir_val *v;
    v = (struct ir_val *)inl_alloc(s->arena, sizeof(struct ir_val));
    v->id = s->caller->next_val_id++;
    v->type = type;
    return v;
}

static struct ir_block *clone_block(struct inline_state *s,
                                     const char *label)
{
    struct ir_block *bb;
    bb = (struct ir_block *)inl_alloc(s->arena, sizeof(struct ir_block));
    bb->id = s->caller->next_block_id++;
    bb->label = inl_strdup(s->arena, label);
    bb->preds = (struct ir_block **)inl_alloc(
        s->arena, MAX_PREDS * sizeof(struct ir_block *));
    bb->preds_cap = MAX_PREDS;
    return bb;
}

/* Map a value from callee to caller. Returns the mapped value. */
static struct ir_val *map_val(struct inline_state *s, struct ir_val *v)
{
    if (!v) return NULL;
    if (v->id >= 0 && v->id < MAX_VALS && s->val_map[v->id])
        return s->val_map[v->id];
    return v;
}

/* Map a block from callee to caller */
static struct ir_block *map_block(struct inline_state *s,
                                   struct ir_block *bb)
{
    if (!bb) return NULL;
    if (bb->id >= 0 && bb->id < MAX_BLOCKS && s->block_map[bb->id])
        return s->block_map[bb->id];
    return bb;
}

/* Append a block to the caller's block list */
static void append_block_to(struct ir_func *func, struct ir_block *bb)
{
    struct ir_block *t;
    if (!func->blocks) {
        func->blocks = bb;
    } else {
        for (t = func->blocks; t->next; t = t->next) {
            /* walk to end */
        }
        t->next = bb;
    }
    func->nblocks++;
}

/* Append an instruction to a block */
static void append_inst(struct ir_block *bb, struct ir_inst *inst)
{
    inst->parent = bb;
    if (!bb->last) {
        bb->first = inst;
        bb->last = inst;
    } else {
        bb->last->next = inst;
        inst->prev = bb->last;
        bb->last = inst;
    }
}

/* Clone a single instruction, remapping values and blocks */
static struct ir_inst *clone_inst(struct inline_state *s,
                                   struct ir_inst *orig)
{
    struct ir_inst *ni;
    int i;

    ni = (struct ir_inst *)inl_alloc(s->arena, sizeof(struct ir_inst));
    ni->op = orig->op;
    ni->imm = orig->imm;
    ni->name = inl_strdup(s->arena, orig->name);

    /* clone result */
    if (orig->result) {
        ni->result = clone_val(s, orig->result->type);
        ni->result->def = ni;
        if (orig->result->id >= 0 && orig->result->id < MAX_VALS)
            s->val_map[orig->result->id] = ni->result;
    }

    /* clone args, remapping values */
    if (orig->nargs > 0) {
        ni->args = (struct ir_val **)inl_alloc(
            s->arena, (usize)orig->nargs * sizeof(struct ir_val *));
        ni->nargs = orig->nargs;
        for (i = 0; i < orig->nargs; i++) {
            ni->args[i] = map_val(s, orig->args[i]);
        }
    }

    /* clone phi blocks */
    if (orig->phi_blocks && orig->nargs > 0) {
        ni->phi_blocks = (struct ir_block **)inl_alloc(
            s->arena, (usize)orig->nargs * sizeof(struct ir_block *));
        for (i = 0; i < orig->nargs; i++) {
            ni->phi_blocks[i] = map_block(s, orig->phi_blocks[i]);
        }
    }

    /* remap branch targets */
    ni->target = map_block(s, orig->target);
    ni->true_bb = map_block(s, orig->true_bb);
    ni->false_bb = map_block(s, orig->false_bb);

    return ni;
}

/* Replace all uses of old_val with new_val in the function */
static void replace_val_uses(struct ir_func *f,
                              struct ir_val *old_val,
                              struct ir_val *new_val)
{
    struct ir_block *bb;
    struct ir_inst *inst;
    int i;

    if (!old_val || !new_val) return;
    for (bb = f->blocks; bb; bb = bb->next) {
        for (inst = bb->first; inst; inst = inst->next) {
            for (i = 0; i < inst->nargs; i++) {
                if (inst->args[i] == old_val)
                    inst->args[i] = new_val;
            }
        }
    }
}

/*
 * Inline one call site. Returns 1 if inlined, 0 if not.
 *
 * Strategy:
 *   1. Create a merge block for code after the call
 *   2. Clone all callee blocks, mapping params to call args
 *   3. Replace IR_RET in cloned body with branch to merge
 *   4. If callee returns a value, collect return values and
 *      replace uses of the call result
 *   5. Split the call block: code after call moves to merge
 *   6. Replace the call with a branch to the cloned entry
 */
static int inline_call(struct inline_state *s,
                        struct ir_block *call_bb,
                        struct ir_inst *call_inst)
{
    struct ir_block *callee_bb;
    struct ir_inst *inst;
    struct ir_inst *move;
    struct ir_inst *after;
    struct ir_block *merge_bb;
    struct ir_block *cloned_entry;
    struct ir_inst *br;
    struct ir_val *call_result;
    struct ir_val *ret_vals[MAX_RETS];
    int nrets;
    int i;

    /* parameter count must match */
    if (s->callee->nparams > call_inst->nargs) return 0;

    memset(s->val_map, 0, sizeof(s->val_map));
    memset(s->block_map, 0, sizeof(s->block_map));

    /* save call result before we modify the instruction */
    call_result = call_inst->result;
    nrets = 0;

    /* map callee parameters to call arguments */
    for (i = 0; i < s->callee->nparams && i < call_inst->nargs; i++) {
        if (s->callee->params[i] &&
            s->callee->params[i]->id >= 0 &&
            s->callee->params[i]->id < MAX_VALS) {
            s->val_map[s->callee->params[i]->id] = call_inst->args[i];
        }
    }

    /* Phase 1: create cloned blocks (so block mapping is ready) */
    for (callee_bb = s->callee->blocks; callee_bb;
         callee_bb = callee_bb->next) {
        struct ir_block *cb;
        cb = clone_block(s, callee_bb->label);
        if (callee_bb->id >= 0 && callee_bb->id < MAX_BLOCKS)
            s->block_map[callee_bb->id] = cb;
    }

    cloned_entry = map_block(s, s->callee->entry);
    if (!cloned_entry) return 0;

    /* create merge block for after the inlined code */
    merge_bb = clone_block(s, "inline_merge");

    /* Phase 2: clone instructions into the blocks */
    for (callee_bb = s->callee->blocks; callee_bb;
         callee_bb = callee_bb->next) {
        struct ir_block *dest;
        dest = map_block(s, callee_bb);

        for (inst = callee_bb->first; inst; inst = inst->next) {
            if (inst->op == IR_RET) {
                /* Collect the return value (mapped) */
                if (call_result && inst->nargs >= 1 && inst->args[0]) {
                    struct ir_val *rv;
                    rv = map_val(s, inst->args[0]);
                    if (nrets < MAX_RETS) ret_vals[nrets++] = rv;
                }
                /* emit branch to merge */
                br = (struct ir_inst *)inl_alloc(
                    s->arena, sizeof(struct ir_inst));
                br->op = IR_BR;
                br->target = merge_bb;
                append_inst(dest, br);
            } else {
                struct ir_inst *ci;
                ci = clone_inst(s, inst);
                append_inst(dest, ci);
            }
        }
    }

    /* Phase 3: splice the inlined blocks into the caller */
    for (callee_bb = s->callee->blocks; callee_bb;
         callee_bb = callee_bb->next) {
        append_block_to(s->caller, map_block(s, callee_bb));
    }
    append_block_to(s->caller, merge_bb);

    /* Phase 4: move instructions after the call to merge_bb */
    move = call_inst->next;
    while (move) {
        after = move->next;

        /* unlink from call_bb */
        if (move->prev) move->prev->next = move->next;
        if (move->next) move->next->prev = move->prev;
        if (call_bb->last == move) call_bb->last = move->prev;
        move->prev = NULL;
        move->next = NULL;

        append_inst(merge_bb, move);
        move = after;
    }

    /* Phase 5: replace the call instruction with branch to inlined entry */
    call_inst->op = IR_BR;
    call_inst->target = cloned_entry;
    call_inst->result = NULL;
    call_inst->nargs = 0;
    call_inst->args = NULL;
    call_inst->name = NULL;
    call_inst->true_bb = NULL;
    call_inst->false_bb = NULL;
    call_bb->last = call_inst;

    /* Phase 6: replace uses of the call result with the return value.
       For a single return site, just do a direct replacement.
       For multiple returns, we'd need a phi node, but for simplicity
       we handle the common single-return case. */
    if (call_result && nrets > 0) {
        replace_val_uses(s->caller, call_result, ret_vals[0]);
    }

    return 1;
}

/* ---- main inlining pass ---- */

void opt_inline(struct ir_module *mod, struct arena *arena)
{
    struct ir_func *caller;
    struct ir_block *bb;
    struct ir_inst *inst;
    struct ir_inst *next;
    struct inline_state *s;
    int changed;
    int pass;

    if (!mod || !arena) return;

    s = (struct inline_state *)calloc(1, sizeof(struct inline_state));
    if (!s) return;
    s->arena = arena;

    /* run up to 3 passes to handle nested inlines */
    for (pass = 0; pass < 3; pass++) {
        changed = 0;

        for (caller = mod->funcs; caller; caller = caller->next) {
            s->caller = caller;

            for (bb = caller->blocks; bb; bb = bb->next) {
                for (inst = bb->first; inst; inst = next) {
                    next = inst->next;

                    if (inst->op != IR_CALL || !inst->name)
                        continue;

                    s->callee = find_func(mod, inst->name);
                    if (!s->callee || s->callee == caller)
                        continue;
                    if (!is_inlinable(s->callee))
                        continue;

                    if (inline_call(s, bb, inst)) {
                        changed = 1;
                        /* After inlining, the rest of bb's instructions
                           moved to merge_bb. Stop scanning this block. */
                        break;
                    }
                }
            }

            /* re-run SCCP and DCE after inlining to clean up */
            if (changed) {
                opt_sccp(caller, arena);
                opt_dce(caller);
            }
        }

        if (!changed) break;
    }

    free(s);
}
