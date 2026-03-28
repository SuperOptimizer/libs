/*
 * lto.c - Link-Time Optimization for the free toolchain.
 * Merges IR modules and performs whole-program optimizations:
 * internalization, dead function/global elimination, constant
 * propagation, function merging, and local DCE.
 * Pure C89.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "free.h"
#include "ir.h"

/* ---- helpers ---- */

static int name_eq(const char *a, const char *b)
{
    if (a == NULL || b == NULL) { return a == b; }
    return strcmp(a, b) == 0;
}

/*
 * count_func_insts - count IR instructions in a function.
 * Reserved for future use (inline cost model).
 */
#if 0
static int count_func_insts(struct ir_func *fn)
{
    struct ir_block *bb;
    struct ir_inst *it;
    int n;

    n = 0;
    for (bb = fn->blocks; bb != NULL; bb = bb->next)
        for (it = bb->first; it != NULL; it = it->next)
            n++;
    return n;
}
#endif

static struct ir_func *find_func(struct ir_module *mod, const char *name)
{
    struct ir_func *fn;
    for (fn = mod->funcs; fn != NULL; fn = fn->next)
        if (name_eq(fn->name, name)) return fn;
    return NULL;
}

static struct ir_global *find_global(struct ir_module *mod, const char *name)
{
    struct ir_global *g;
    for (g = mod->globals; g != NULL; g = g->next)
        if (name_eq(g->name, name)) return g;
    return NULL;
}

static int func_is_decl(struct ir_func *fn)
{
    return fn->blocks == NULL;
}

/* ---- merge ---- */

/*
 * lto_merge - Merge multiple IR modules into one.
 * Transfers ownership of linked-list nodes directly (no deep copy needed
 * because all nodes are arena-allocated in the LTO arena).
 * Prefers definitions over declarations for duplicate symbols.
 */
struct ir_module *lto_merge(struct ir_module **modules, int nmodules,
                            struct arena *a)
{
    struct ir_module *m;
    int mi;
    struct ir_func *fn_tail;
    struct ir_global *g_tail;

    m = (struct ir_module *)arena_alloc(a, sizeof(struct ir_module));
    memset(m, 0, sizeof(*m));
    m->arena = a;
    fn_tail = NULL;
    g_tail = NULL;

    for (mi = 0; mi < nmodules; mi++) {
        struct ir_func *sf;
        struct ir_func *sf_next;
        struct ir_global *sg;
        struct ir_global *sg_next;

        /* splice functions */
        for (sf = modules[mi]->funcs; sf != NULL; sf = sf_next) {
            struct ir_func *ex;
            sf_next = sf->next;
            sf->next = NULL;
            ex = find_func(m, sf->name);
            if (ex != NULL) {
                if (func_is_decl(ex) && !func_is_decl(sf)) {
                    /* replace declaration with definition in-place */
                    sf->next = ex->next;
                    *ex = *sf;
                }
            } else {
                if (fn_tail == NULL) { m->funcs = sf; }
                else { fn_tail->next = sf; }
                fn_tail = sf;
            }
        }
        modules[mi]->funcs = NULL;

        /* splice globals */
        for (sg = modules[mi]->globals; sg != NULL; sg = sg_next) {
            sg_next = sg->next;
            sg->next = NULL;
            if (find_global(m, sg->name) == NULL) {
                if (g_tail == NULL) { m->globals = sg; }
                else { g_tail->next = sg; }
                g_tail = sg;
            }
        }
        modules[mi]->globals = NULL;
    }
    return m;
}

/* ---- dead function elimination ---- */

#define MAX_REACH 1024

static void mark_reach(struct ir_module *mod, struct ir_func *fn,
                       char *vis, int max)
{
    struct ir_func *cur;
    struct ir_block *bb;
    struct ir_inst *it;
    int idx;

    idx = 0;
    for (cur = mod->funcs; cur != fn && cur != NULL; cur = cur->next)
        idx++;
    if (idx >= max || vis[idx]) return;
    vis[idx] = 1;

    for (bb = fn->blocks; bb != NULL; bb = bb->next)
        for (it = bb->first; it != NULL; it = it->next)
            if (it->op == IR_CALL && it->name != NULL) {
                struct ir_func *c;
                c = find_func(mod, it->name);
                if (c != NULL) mark_reach(mod, c, vis, max);
            }
}

static void elim_dead_funcs(struct ir_module *mod)
{
    char vis[MAX_REACH];
    struct ir_func *fn;
    struct ir_func *prev;
    struct ir_func *next;
    int nf;
    int idx;

    memset(vis, 0, sizeof(vis));
    nf = 0;
    for (fn = mod->funcs; fn != NULL; fn = fn->next) nf++;
    if (nf > MAX_REACH) return;

    fn = find_func(mod, "main");
    if (fn != NULL) mark_reach(mod, fn, vis, nf);
    fn = find_func(mod, "_start");
    if (fn != NULL) mark_reach(mod, fn, vis, nf);

    prev = NULL;
    idx = 0;
    for (fn = mod->funcs; fn != NULL; fn = next, idx++) {
        next = fn->next;
        if (!vis[idx] && func_is_decl(fn)) {
            if (prev != NULL) prev->next = next;
            else mod->funcs = next;
        } else {
            prev = fn;
        }
    }
}

/* ---- dead global elimination ---- */

static void elim_dead_globals(struct ir_module *mod)
{
    char ref[MAX_REACH];
    struct ir_func *fn;
    struct ir_block *bb;
    struct ir_inst *it;
    struct ir_global *g;
    struct ir_global *prev;
    struct ir_global *next;
    int ng;
    int idx;

    ng = 0;
    for (g = mod->globals; g != NULL; g = g->next) ng++;
    if (ng > MAX_REACH) return;
    memset(ref, 0, sizeof(ref));

    for (fn = mod->funcs; fn != NULL; fn = fn->next)
        for (bb = fn->blocks; bb != NULL; bb = bb->next)
            for (it = bb->first; it != NULL; it = it->next)
                if (it->op == IR_GLOBAL_ADDR && it->name != NULL) {
                    idx = 0;
                    for (g = mod->globals; g != NULL; g = g->next, idx++)
                        if (name_eq(g->name, it->name)) {
                            ref[idx] = 1; break;
                        }
                }

    prev = NULL;
    idx = 0;
    for (g = mod->globals; g != NULL; g = next, idx++) {
        next = g->next;
        if (!ref[idx]) {
            if (prev != NULL) prev->next = next;
            else mod->globals = next;
        } else {
            prev = g;
        }
    }
}

/* ---- whole-program constant propagation ---- */

static int prop_global_const(struct ir_module *mod)
{
    struct ir_global *g;
    struct ir_func *fn;
    struct ir_block *bb;
    struct ir_inst *it;
    int changed;
    int writes;

    changed = 0;
    for (g = mod->globals; g != NULL; g = g->next) {
        if (g->init_val == 0) continue;
        writes = 0;
        for (fn = mod->funcs; fn != NULL && writes == 0; fn = fn->next)
            for (bb = fn->blocks; bb != NULL && writes == 0; bb = bb->next)
                for (it = bb->first; it != NULL; it = it->next)
                    if (it->op == IR_STORE && name_eq(it->name, g->name))
                        writes++;

        if (writes == 0) {
            for (fn = mod->funcs; fn != NULL; fn = fn->next)
                for (bb = fn->blocks; bb != NULL; bb = bb->next)
                    for (it = bb->first; it != NULL; it = it->next)
                        if (it->op == IR_LOAD &&
                            name_eq(it->name, g->name)) {
                            it->op = IR_CONST;
                            it->imm = g->init_val;
                            it->name = NULL;
                            it->nargs = 0;
                            changed = 1;
                        }
        }
    }
    return changed;
}

/* ---- function merging ---- */

static int funcs_identical(struct ir_func *a, struct ir_func *b)
{
    struct ir_block *ba;
    struct ir_block *bb_blk;
    struct ir_inst *ia;
    struct ir_inst *ib;

    if (a->nblocks != b->nblocks || a->ret_type != b->ret_type ||
        a->nparams != b->nparams)
        return 0;

    ba = a->blocks;
    bb_blk = b->blocks;
    while (ba != NULL && bb_blk != NULL) {
        ia = ba->first;
        ib = bb_blk->first;
        while (ia != NULL && ib != NULL) {
            int oi;
            if (ia->op != ib->op || ia->imm != ib->imm ||
                ia->nargs != ib->nargs)
                return 0;
            if ((ia->result != NULL) != (ib->result != NULL))
                return 0;
            if (ia->result && ib->result &&
                ia->result->type != ib->result->type)
                return 0;
            for (oi = 0; oi < ia->nargs; oi++)
                if (ia->args[oi] && ib->args[oi] &&
                    ia->args[oi]->id != ib->args[oi]->id)
                    return 0;
            ia = ia->next;
            ib = ib->next;
        }
        if (ia != NULL || ib != NULL) return 0;
        ba = ba->next;
        bb_blk = bb_blk->next;
    }
    return ba == NULL && bb_blk == NULL;
}

static int merge_funcs(struct ir_module *mod)
{
    struct ir_func *fa;
    struct ir_func *fb;
    struct ir_func *fn;
    struct ir_block *bb;
    struct ir_inst *it;
    int merged;

    merged = 0;
    for (fa = mod->funcs; fa != NULL; fa = fa->next) {
        if (func_is_decl(fa)) continue;
        for (fb = fa->next; fb != NULL; fb = fb->next) {
            if (func_is_decl(fb) || !funcs_identical(fa, fb))
                continue;
            for (fn = mod->funcs; fn != NULL; fn = fn->next)
                for (bb = fn->blocks; bb != NULL; bb = bb->next)
                    for (it = bb->first; it != NULL; it = it->next)
                        if (it->op == IR_CALL &&
                            name_eq(it->name, fb->name))
                            it->name = fa->name;
            fb->blocks = NULL;
            fb->entry = NULL;
            fb->nblocks = 0;
            merged++;
        }
    }
    return merged;
}

/* ---- local DCE ---- */

static int local_dce(struct ir_module *mod)
{
    struct ir_func *fn;
    struct ir_block *bb;
    struct ir_block *cb;
    struct ir_inst *it;
    struct ir_inst *chk;
    int changed;
    int used;
    int oi;

    changed = 0;
    for (fn = mod->funcs; fn != NULL; fn = fn->next) {
        if (func_is_decl(fn)) continue;
        for (bb = fn->blocks; bb != NULL; bb = bb->next) {
            for (it = bb->first; it != NULL; it = it->next) {
                if (it->result == NULL) continue;
                if (it->op == IR_STORE || it->op == IR_CALL ||
                    it->op == IR_BR || it->op == IR_BR_COND ||
                    it->op == IR_RET)
                    continue;
                used = 0;
                for (cb = fn->blocks; cb && !used; cb = cb->next)
                    for (chk = cb->first; chk && !used; chk = chk->next)
                        for (oi = 0; oi < chk->nargs; oi++)
                            if (chk->args[oi] &&
                                chk->args[oi]->id == it->result->id) {
                                used = 1; break;
                            }
                if (!used) {
                    if (it->prev) it->prev->next = it->next;
                    else bb->first = it->next;
                    if (it->next) it->next->prev = it->prev;
                    else bb->last = it->prev;
                    changed = 1;
                }
            }
        }
    }
    return changed;
}

/* ---- main LTO pipeline ---- */

void lto_optimize(struct ir_module *merged)
{
    int pass;
    int work;

    if (merged == NULL) return;

    for (pass = 0; pass < 4; pass++) {
        work = 0;
        elim_dead_funcs(merged);
        elim_dead_globals(merged);
        work |= prop_global_const(merged);
        work |= merge_funcs(merged);
        work |= local_dce(merged);
        if (!work) break;
    }
}
