/*
 * ir.c - Build SSA IR from the AST. Pure C89.
 * Local variables use alloca + load/store (mem2reg promotes later).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "free.h"
#include "ir.h"

#define MAX_LOCALS 256
#define MAX_ARGS   8
#define MAX_PREDS  32

struct local_entry { char *name; int offset; struct ir_val *alloca_v; };

struct ir_builder {
    struct arena *arena; struct ir_func *func; struct ir_block *cur_bb;
    struct local_entry locals[MAX_LOCALS]; int nlocals;
    struct ir_block *break_bb; struct ir_block *continue_bb;
};

static struct ir_val *build_expr(struct ir_builder *b, struct node *n);
static void build_stmt(struct ir_builder *b, struct node *n);

static void *ir_alloc(struct ir_builder *b, usize sz)
{ void *p; p = arena_alloc(b->arena, sz); memset(p, 0, sz); return p; }

static char *ir_strdup(struct ir_builder *b, const char *s)
{ return s ? str_dup(b->arena, s, (int)strlen(s)) : NULL; }

static enum ir_type_kind type_to_irt(struct type *ty)
{
    if (!ty) return IRT_I64;
    switch (ty->kind) {
    case TY_VOID: return IRT_VOID; case TY_BOOL: return IRT_I1;
    case TY_CHAR: return IRT_I8;   case TY_SHORT: return IRT_I16;
    case TY_INT: case TY_ENUM: return IRT_I32;
    case TY_LONG: case TY_LLONG: return IRT_I64;
    case TY_PTR: case TY_ARRAY: case TY_STRUCT: case TY_UNION:
    case TY_FUNC: return IRT_PTR;
    default: return IRT_I64;
    }
}

static int irt_size(enum ir_type_kind t)
{
    switch (t) {
    case IRT_VOID: return 0; case IRT_I1: case IRT_I8: return 1;
    case IRT_I16: return 2; case IRT_I32: return 4; default: return 8;
    }
}

static int is_agg(struct type *ty)
{ return ty && (ty->kind == TY_ARRAY || ty->kind == TY_STRUCT || ty->kind == TY_UNION); }

static struct ir_val *new_val(struct ir_builder *b, enum ir_type_kind t)
{
    struct ir_val *v;
    v = (struct ir_val *)ir_alloc(b, sizeof(struct ir_val));
    v->id = b->func->next_val_id++; v->type = t;
    return v;
}

static struct ir_block *new_block(struct ir_builder *b, const char *lbl)
{
    struct ir_block *bb;
    bb = (struct ir_block *)ir_alloc(b, sizeof(struct ir_block));
    bb->id = b->func->next_block_id++;
    bb->label = lbl ? ir_strdup(b, lbl) : NULL;
    bb->preds = (struct ir_block **)ir_alloc(b, MAX_PREDS * sizeof(struct ir_block *));
    bb->preds_cap = MAX_PREDS;
    return bb;
}

static void append_block(struct ir_builder *b, struct ir_block *bb)
{
    struct ir_block *t;
    if (!b->func->blocks) b->func->blocks = bb;
    else { for (t = b->func->blocks; t->next; t = t->next) {} t->next = bb; }
    b->func->nblocks++;
}

static void set_succs(struct ir_builder *b, struct ir_block *from,
                       struct ir_block *s1, struct ir_block *s2)
{
    int n; n = s2 ? 2 : 1;
    from->succs = (struct ir_block **)ir_alloc(b, (usize)n * sizeof(struct ir_block *));
    from->succs[0] = s1; if (s2) from->succs[1] = s2; from->nsuccs = n;
    if (s1->npreds < s1->preds_cap) s1->preds[s1->npreds++] = from;
    if (s2 && s2->npreds < s2->preds_cap) s2->preds[s2->npreds++] = from;
}

static struct ir_inst *new_inst(struct ir_builder *b, enum ir_op op)
{
    struct ir_inst *i;
    i = (struct ir_inst *)ir_alloc(b, sizeof(struct ir_inst)); i->op = op;
    return i;
}

static void emit_inst(struct ir_builder *b, struct ir_inst *inst)
{
    struct ir_block *bb; bb = b->cur_bb;
    if (!bb) return;
    inst->parent = bb;
    if (!bb->last) { bb->first = inst; bb->last = inst; }
    else { bb->last->next = inst; inst->prev = bb->last; bb->last = inst; }
}

static int block_terminated(struct ir_builder *b)
{
    struct ir_inst *l;
    if (!b->cur_bb) return 1;
    l = b->cur_bb->last; if (!l) return 0;
    return l->op == IR_BR || l->op == IR_BR_COND || l->op == IR_RET;
}

static struct ir_val *emit_const(struct ir_builder *b, enum ir_type_kind t, long val)
{
    struct ir_inst *i; struct ir_val *v;
    v = new_val(b, t); i = new_inst(b, IR_CONST);
    i->result = v; i->imm = val; v->def = i; emit_inst(b, i); return v;
}

static struct ir_val *emit_global_addr(struct ir_builder *b, const char *nm)
{
    struct ir_inst *i; struct ir_val *v;
    v = new_val(b, IRT_PTR); i = new_inst(b, IR_GLOBAL_ADDR);
    i->result = v; i->name = ir_strdup(b, nm); v->def = i; emit_inst(b, i); return v;
}

static struct ir_val *emit_alloca(struct ir_builder *b, int size)
{
    struct ir_inst *i; struct ir_val *v;
    v = new_val(b, IRT_PTR); i = new_inst(b, IR_ALLOCA);
    i->result = v; i->imm = (long)size; v->def = i; emit_inst(b, i); return v;
}

static struct ir_val *emit_load(struct ir_builder *b, enum ir_type_kind t, struct ir_val *ptr)
{
    struct ir_inst *i; struct ir_val *v;
    v = new_val(b, t); i = new_inst(b, IR_LOAD); i->result = v;
    i->args = (struct ir_val **)ir_alloc(b, sizeof(struct ir_val *));
    i->args[0] = ptr; i->nargs = 1; v->def = i; emit_inst(b, i); return v;
}

static void emit_store(struct ir_builder *b, struct ir_val *val, struct ir_val *ptr)
{
    struct ir_inst *i; i = new_inst(b, IR_STORE);
    i->args = (struct ir_val **)ir_alloc(b, 2 * sizeof(struct ir_val *));
    i->args[0] = val; i->args[1] = ptr; i->nargs = 2; emit_inst(b, i);
}

static struct ir_val *emit_binop(struct ir_builder *b, enum ir_op op,
                                  enum ir_type_kind t, struct ir_val *l, struct ir_val *r)
{
    struct ir_inst *i; struct ir_val *v;
    v = new_val(b, t); i = new_inst(b, op); i->result = v;
    i->args = (struct ir_val **)ir_alloc(b, 2 * sizeof(struct ir_val *));
    i->args[0] = l; i->args[1] = r; i->nargs = 2;
    v->def = i; emit_inst(b, i); return v;
}

static struct ir_val *emit_unop(struct ir_builder *b, enum ir_op op,
                                 enum ir_type_kind t, struct ir_val *arg)
{
    struct ir_inst *i; struct ir_val *v;
    v = new_val(b, t); i = new_inst(b, op); i->result = v;
    i->args = (struct ir_val **)ir_alloc(b, sizeof(struct ir_val *));
    i->args[0] = arg; i->nargs = 1; v->def = i; emit_inst(b, i); return v;
}

static void emit_br(struct ir_builder *b, struct ir_block *tgt)
{
    struct ir_inst *i;
    if (block_terminated(b)) return;
    i = new_inst(b, IR_BR); i->target = tgt;
    emit_inst(b, i); set_succs(b, b->cur_bb, tgt, NULL);
}

static void emit_br_cond(struct ir_builder *b, struct ir_val *c,
                           struct ir_block *tb, struct ir_block *fb)
{
    struct ir_inst *i;
    if (block_terminated(b)) return;
    i = new_inst(b, IR_BR_COND);
    i->args = (struct ir_val **)ir_alloc(b, sizeof(struct ir_val *));
    i->args[0] = c; i->nargs = 1; i->true_bb = tb; i->false_bb = fb;
    emit_inst(b, i); set_succs(b, b->cur_bb, tb, fb);
}

static void emit_ret(struct ir_builder *b, struct ir_val *v)
{
    struct ir_inst *i;
    if (block_terminated(b)) return;
    i = new_inst(b, IR_RET);
    if (v) { i->args = (struct ir_val **)ir_alloc(b, sizeof(struct ir_val *));
             i->args[0] = v; i->nargs = 1; }
    emit_inst(b, i);
}

static struct ir_val *emit_call(struct ir_builder *b, const char *fn,
                                 enum ir_type_kind rt, struct ir_val **ca, int na)
{
    struct ir_inst *i; struct ir_val *v; int j;
    i = new_inst(b, IR_CALL); i->name = ir_strdup(b, fn);
    if (na > 0) {
        i->args = (struct ir_val **)ir_alloc(b, (usize)na * sizeof(struct ir_val *));
        for (j = 0; j < na; j++) i->args[j] = ca[j];
        i->nargs = na;
    }
    if (rt != IRT_VOID) { v = new_val(b, rt); i->result = v; v->def = i; }
    else v = NULL;
    emit_inst(b, i); return v;
}

static struct ir_val *emit_phi2(struct ir_builder *b, enum ir_type_kind t,
                                 struct ir_val *v1, struct ir_block *b1,
                                 struct ir_val *v2, struct ir_block *b2)
{
    struct ir_inst *p; struct ir_val *pv;
    pv = new_val(b, t); p = new_inst(b, IR_PHI); p->result = pv; pv->def = p;
    p->args = (struct ir_val **)ir_alloc(b, 2 * sizeof(struct ir_val *));
    p->args[0] = v1; p->args[1] = v2; p->nargs = 2;
    p->phi_blocks = (struct ir_block **)ir_alloc(b, 2 * sizeof(struct ir_block *));
    p->phi_blocks[0] = b1; p->phi_blocks[1] = b2;
    emit_inst(b, p); return pv;
}

/* ---- local variable helpers ---- */

static struct ir_val *find_local(struct ir_builder *b,
                                  const char *name, int offset)
{
    int i;
    for (i = 0; i < b->nlocals; i++) {
        if (offset != 0 && b->locals[i].offset == offset)
            return b->locals[i].alloca_v;
        if (name && b->locals[i].name && strcmp(b->locals[i].name, name) == 0)
            return b->locals[i].alloca_v;
    }
    return NULL;
}

static void add_local(struct ir_builder *b, const char *name,
                       int offset, struct ir_val *av)
{
    if (b->nlocals >= MAX_LOCALS) return;
    b->locals[b->nlocals].name = ir_strdup(b, name);
    b->locals[b->nlocals].offset = offset;
    b->locals[b->nlocals].alloca_v = av;
    b->nlocals++;
}

static struct ir_val *emit_cmp_zero(struct ir_builder *b, struct ir_val *v)
{
    struct ir_val *z;
    z = emit_const(b, v->type, 0);
    return emit_binop(b, IR_NE, IRT_I1, v, z);
}

/* start a dead block after terminator */
static void start_dead(struct ir_builder *b, const char *label)
{
    struct ir_block *d;
    d = new_block(b, label);
    append_block(b, d);
    b->cur_bb = d;
}

/* ---- address generation ---- */

static struct ir_val *build_addr(struct ir_builder *b, struct node *n)
{
    struct ir_val *base_v;
    struct ir_val *off_v;
    struct ir_val *a;

    if (n == NULL) return emit_const(b, IRT_PTR, 0);
    switch (n->kind) {
    case ND_VAR:
        if (n->offset != 0) {
            a = find_local(b, n->name, n->offset);
            if (a == NULL) {
                a = emit_alloca(b, n->ty ? n->ty->size : 8);
                add_local(b, n->name, n->offset, a);
            }
            return a;
        }
        return emit_global_addr(b, n->name);
    case ND_DEREF:
        return build_expr(b, n->lhs);
    case ND_MEMBER:
        base_v = build_addr(b, n->lhs);
        if (n->offset != 0) {
            off_v = emit_const(b, IRT_I64, (long)n->offset);
            return emit_binop(b, IR_ADD, IRT_PTR, base_v, off_v);
        }
        return base_v;
    default:
        return build_expr(b, n);
    }
}

/* ---- expression generation ---- */

static struct ir_val *build_expr(struct ir_builder *b, struct node *n)
{
    struct ir_val *lv, *rv, *av, *vv, *cv;
    struct ir_val *cargs[MAX_ARGS];
    enum ir_type_kind rt;
    struct ir_block *tb, *fb, *mb, *rb;
    struct node *arg;
    int na, step;

    if (n == NULL) return emit_const(b, IRT_I64, 0);
    rt = type_to_irt(n->ty);

    switch (n->kind) {
    case ND_NUM:
        return emit_const(b, rt, n->val);

    case ND_VAR:
        av = find_local(b, n->name, n->offset);
        if (n->offset != 0) {
            if (av == NULL) {
                av = emit_alloca(b, irt_size(rt));
                add_local(b, n->name, n->offset, av);
            }
        } else {
            av = emit_global_addr(b, n->name);
        }
        return is_agg(n->ty) ? av : emit_load(b, rt, av);

    case ND_STR:
        return emit_global_addr(b, n->name ? n->name : "");
    case ND_ASSIGN:
        rv = build_expr(b, n->rhs);
        av = build_addr(b, n->lhs);
        emit_store(b, rv, av);
        return rv;
    case ND_ADDR:
        return build_addr(b, n->lhs);
    case ND_DEREF:
        vv = build_expr(b, n->lhs);
        return is_agg(n->ty) ? vv : emit_load(b, rt, vv);
    case ND_MEMBER:
        av = build_addr(b, n);
        return is_agg(n->ty) ? av : emit_load(b, rt, av);

    /* binary arithmetic with pointer scaling */
    case ND_ADD:
        lv = build_expr(b, n->lhs); rv = build_expr(b, n->rhs);
        if (n->ty && n->ty->kind == TY_PTR && n->ty->base &&
            n->ty->base->size > 1) {
            vv = emit_const(b, IRT_I64, (long)n->ty->base->size);
            rv = emit_binop(b, IR_MUL, IRT_I64, rv, vv);
        }
        return emit_binop(b, IR_ADD, rt, lv, rv);
    case ND_SUB:
        lv = build_expr(b, n->lhs); rv = build_expr(b, n->rhs);
        if (n->lhs && n->lhs->ty && n->lhs->ty->kind == TY_PTR &&
            n->lhs->ty->base && n->rhs && n->rhs->ty &&
            n->rhs->ty->kind == TY_PTR) {
            vv = emit_binop(b, IR_SUB, IRT_I64, lv, rv);
            step = n->lhs->ty->base->size;
            if (step > 1) {
                av = emit_const(b, IRT_I64, (long)step);
                vv = emit_binop(b, IR_DIV, IRT_I64, vv, av);
            }
            return vv;
        }
        if (n->ty && n->ty->kind == TY_PTR && n->ty->base &&
            n->ty->base->size > 1) {
            vv = emit_const(b, IRT_I64, (long)n->ty->base->size);
            rv = emit_binop(b, IR_MUL, IRT_I64, rv, vv);
        }
        return emit_binop(b, IR_SUB, rt, lv, rv);

    /* simple binary ops */
    case ND_MUL: case ND_DIV: case ND_MOD:
    case ND_BITAND: case ND_BITOR: case ND_BITXOR:
    case ND_SHL: case ND_SHR:
        lv = build_expr(b, n->lhs); rv = build_expr(b, n->rhs);
        switch (n->kind) {
        case ND_MUL: return emit_binop(b, IR_MUL, rt, lv, rv);
        case ND_DIV: return emit_binop(b, IR_DIV, rt, lv, rv);
        case ND_MOD: return emit_binop(b, IR_MOD, rt, lv, rv);
        case ND_BITAND: return emit_binop(b, IR_AND, rt, lv, rv);
        case ND_BITOR: return emit_binop(b, IR_OR, rt, lv, rv);
        case ND_BITXOR: return emit_binop(b, IR_XOR, rt, lv, rv);
        case ND_SHL: return emit_binop(b, IR_SHL, rt, lv, rv);
        case ND_SHR:
            if (n->ty && n->ty->is_unsigned)
                return emit_binop(b, IR_SHR, rt, lv, rv);
            return emit_binop(b, IR_SAR, rt, lv, rv);
        default: break;
        }
        break;

    case ND_BITNOT:
        return emit_unop(b, IR_NOT, rt, build_expr(b, n->lhs));

    /* comparison */
    case ND_EQ: case ND_NE: case ND_LT: case ND_LE:
        lv = build_expr(b, n->lhs); rv = build_expr(b, n->rhs);
        switch (n->kind) {
        case ND_EQ: return emit_binop(b, IR_EQ, IRT_I1, lv, rv);
        case ND_NE: return emit_binop(b, IR_NE, IRT_I1, lv, rv);
        case ND_LT:
            if (n->lhs && n->lhs->ty && n->lhs->ty->is_unsigned)
                return emit_binop(b, IR_ULT, IRT_I1, lv, rv);
            return emit_binop(b, IR_LT, IRT_I1, lv, rv);
        case ND_LE:
            if (n->lhs && n->lhs->ty && n->lhs->ty->is_unsigned)
                return emit_binop(b, IR_ULE, IRT_I1, lv, rv);
            return emit_binop(b, IR_LE, IRT_I1, lv, rv);
        default: break;
        }
        break;

    /* short-circuit logical AND */
    case ND_LOGAND:
        tb = new_block(b, "land.rhs");
        fb = new_block(b, "land.false");
        mb = new_block(b, "land.end");
        lv = build_expr(b, n->lhs);
        cv = emit_cmp_zero(b, lv);
        emit_br_cond(b, cv, tb, fb);
        append_block(b, tb); b->cur_bb = tb;
        rv = build_expr(b, n->rhs);
        cv = emit_cmp_zero(b, rv);
        rb = b->cur_bb;
        emit_br(b, mb);
        append_block(b, fb); b->cur_bb = fb;
        emit_br(b, mb);
        append_block(b, mb); b->cur_bb = mb;
        vv = emit_const(b, IRT_I32, 0);
        return emit_phi2(b, IRT_I32, cv, rb, vv, fb);

    /* short-circuit logical OR */
    case ND_LOGOR:
        tb = new_block(b, "lor.true");
        fb = new_block(b, "lor.rhs");
        mb = new_block(b, "lor.end");
        lv = build_expr(b, n->lhs);
        cv = emit_cmp_zero(b, lv);
        emit_br_cond(b, cv, tb, fb);
        append_block(b, tb); b->cur_bb = tb;
        emit_br(b, mb);
        append_block(b, fb); b->cur_bb = fb;
        rv = build_expr(b, n->rhs);
        cv = emit_cmp_zero(b, rv);
        rb = b->cur_bb;
        emit_br(b, mb);
        append_block(b, mb); b->cur_bb = mb;
        vv = emit_const(b, IRT_I32, 1);
        return emit_phi2(b, IRT_I32, vv, tb, cv, rb);

    case ND_LOGNOT:
        lv = build_expr(b, n->lhs);
        vv = emit_const(b, IRT_I1, 0);
        return emit_binop(b, IR_EQ, IRT_I1, lv, vv);

    case ND_CALL:
        na = 0;
        for (arg = n->args; arg && na < MAX_ARGS; arg = arg->next)
            cargs[na++] = build_expr(b, arg);
        return emit_call(b, n->name, rt, cargs, na);

    case ND_COMMA_EXPR:
        build_expr(b, n->lhs);
        return build_expr(b, n->rhs);

    case ND_CAST:
        lv = build_expr(b, n->lhs);
        if (n->ty == NULL || n->lhs == NULL || n->lhs->ty == NULL)
            return lv;
        {
            int ss, ds;
            ss = irt_size(type_to_irt(n->lhs->ty));
            ds = irt_size(rt);
            if (ss == ds) return lv;
            if (ds < ss) return emit_unop(b, IR_TRUNC, rt, lv);
            if (n->ty->is_unsigned) return emit_unop(b, IR_ZEXT, rt, lv);
            return emit_unop(b, IR_SEXT, rt, lv);
        }

    case ND_TERNARY:
        tb = new_block(b, "tern.t"); fb = new_block(b, "tern.f");
        mb = new_block(b, "tern.end");
        cv = emit_cmp_zero(b, build_expr(b, n->cond));
        emit_br_cond(b, cv, tb, fb);
        append_block(b, tb); b->cur_bb = tb;
        lv = build_expr(b, n->then_); tb = b->cur_bb; emit_br(b, mb);
        append_block(b, fb); b->cur_bb = fb;
        rv = build_expr(b, n->els); fb = b->cur_bb; emit_br(b, mb);
        append_block(b, mb); b->cur_bb = mb;
        return emit_phi2(b, rt, lv, tb, rv, fb);

    /* pre/post increment/decrement */
    case ND_PRE_INC: case ND_PRE_DEC:
        av = build_addr(b, n->lhs);
        vv = emit_load(b, rt, av);
        step = 1;
        if (n->lhs && n->lhs->ty && n->lhs->ty->kind == TY_PTR &&
            n->lhs->ty->base) step = n->lhs->ty->base->size;
        rv = emit_const(b, rt, (long)step);
        vv = emit_binop(b, n->kind == ND_PRE_INC ? IR_ADD : IR_SUB,
                         rt, vv, rv);
        emit_store(b, vv, av);
        return vv;

    case ND_POST_INC: case ND_POST_DEC:
        av = build_addr(b, n->lhs);
        vv = emit_load(b, rt, av);
        step = 1;
        if (n->lhs && n->lhs->ty && n->lhs->ty->kind == TY_PTR &&
            n->lhs->ty->base) step = n->lhs->ty->base->size;
        rv = emit_const(b, rt, (long)step);
        lv = emit_binop(b, n->kind == ND_POST_INC ? IR_ADD : IR_SUB,
                         rt, vv, rv);
        emit_store(b, lv, av);
        return vv;

    default:
        break;
    }
    return emit_const(b, rt, 0);
}

/* ---- statement generation ---- */

static void build_stmt(struct ir_builder *b, struct node *n)
{
    struct ir_val *vv, *cv;
    struct ir_block *cond_bb, *body_bb, *inc_bb, *merge_bb;
    struct ir_block *then_bb, *else_bb, *saved_brk, *saved_cont;
    struct node *cur;

    if (n == NULL) return;

    switch (n->kind) {
    case ND_RETURN:
        if (n->lhs) emit_ret(b, build_expr(b, n->lhs));
        else emit_ret(b, NULL);
        start_dead(b, "ret.dead");
        return;

    case ND_BLOCK:
        for (cur = n->body; cur; cur = cur->next)
            build_stmt(b, cur);
        return;

    case ND_IF:
        then_bb = new_block(b, "if.then");
        merge_bb = new_block(b, "if.end");
        else_bb = n->els ? new_block(b, "if.else") : merge_bb;
        cv = emit_cmp_zero(b, build_expr(b, n->cond));
        emit_br_cond(b, cv, then_bb, else_bb);
        append_block(b, then_bb); b->cur_bb = then_bb;
        build_stmt(b, n->then_); emit_br(b, merge_bb);
        if (n->els) {
            append_block(b, else_bb); b->cur_bb = else_bb;
            build_stmt(b, n->els); emit_br(b, merge_bb);
        }
        append_block(b, merge_bb); b->cur_bb = merge_bb;
        return;

    case ND_WHILE:
        cond_bb = new_block(b, "while.cond");
        body_bb = new_block(b, "while.body");
        merge_bb = new_block(b, "while.end");
        saved_brk = b->break_bb; saved_cont = b->continue_bb;
        b->break_bb = merge_bb; b->continue_bb = cond_bb;
        emit_br(b, cond_bb);
        append_block(b, cond_bb); b->cur_bb = cond_bb;
        cv = emit_cmp_zero(b, build_expr(b, n->cond));
        emit_br_cond(b, cv, body_bb, merge_bb);
        append_block(b, body_bb); b->cur_bb = body_bb;
        build_stmt(b, n->then_); emit_br(b, cond_bb);
        append_block(b, merge_bb); b->cur_bb = merge_bb;
        b->break_bb = saved_brk; b->continue_bb = saved_cont;
        return;

    case ND_FOR:
        cond_bb = new_block(b, "for.cond");
        body_bb = new_block(b, "for.body");
        inc_bb = new_block(b, "for.inc");
        merge_bb = new_block(b, "for.end");
        saved_brk = b->break_bb; saved_cont = b->continue_bb;
        b->break_bb = merge_bb; b->continue_bb = inc_bb;
        if (n->init) build_stmt(b, n->init);
        emit_br(b, cond_bb);
        append_block(b, cond_bb); b->cur_bb = cond_bb;
        if (n->cond) {
            cv = emit_cmp_zero(b, build_expr(b, n->cond));
            emit_br_cond(b, cv, body_bb, merge_bb);
        } else { emit_br(b, body_bb); }
        append_block(b, body_bb); b->cur_bb = body_bb;
        build_stmt(b, n->then_); emit_br(b, inc_bb);
        append_block(b, inc_bb); b->cur_bb = inc_bb;
        if (n->inc) build_expr(b, n->inc);
        emit_br(b, cond_bb);
        append_block(b, merge_bb); b->cur_bb = merge_bb;
        b->break_bb = saved_brk; b->continue_bb = saved_cont;
        return;

    case ND_DO:
        body_bb = new_block(b, "do.body");
        cond_bb = new_block(b, "do.cond");
        merge_bb = new_block(b, "do.end");
        saved_brk = b->break_bb; saved_cont = b->continue_bb;
        b->break_bb = merge_bb; b->continue_bb = cond_bb;
        emit_br(b, body_bb);
        append_block(b, body_bb); b->cur_bb = body_bb;
        build_stmt(b, n->then_); emit_br(b, cond_bb);
        append_block(b, cond_bb); b->cur_bb = cond_bb;
        cv = emit_cmp_zero(b, build_expr(b, n->cond));
        emit_br_cond(b, cv, body_bb, merge_bb);
        append_block(b, merge_bb); b->cur_bb = merge_bb;
        b->break_bb = saved_brk; b->continue_bb = saved_cont;
        return;

    case ND_BREAK:
        if (b->break_bb) emit_br(b, b->break_bb);
        start_dead(b, "break.dead");
        return;

    case ND_CONTINUE:
        if (b->continue_bb) emit_br(b, b->continue_bb);
        start_dead(b, "cont.dead");
        return;

    case ND_SWITCH:
        merge_bb = new_block(b, "switch.end");
        saved_brk = b->break_bb; b->break_bb = merge_bb;
        vv = build_expr(b, n->cond);
        for (cur = n->body; cur; cur = cur->next) {
            if (cur->kind == ND_CASE) {
                body_bb = new_block(b, "case");
                else_bb = new_block(b, "case.next");
                cv = emit_const(b, vv->type, cur->val);
                cv = emit_binop(b, IR_EQ, IRT_I1, vv, cv);
                emit_br_cond(b, cv, body_bb, else_bb);
                append_block(b, body_bb); b->cur_bb = body_bb;
                build_stmt(b, cur->lhs); emit_br(b, else_bb);
                append_block(b, else_bb); b->cur_bb = else_bb;
            } else { build_stmt(b, cur); }
        }
        emit_br(b, merge_bb);
        append_block(b, merge_bb); b->cur_bb = merge_bb;
        b->break_bb = saved_brk;
        return;

    case ND_LABEL:
        body_bb = new_block(b, n->name);
        emit_br(b, body_bb);
        append_block(b, body_bb); b->cur_bb = body_bb;
        build_stmt(b, n->lhs);
        return;

    case ND_GOTO:
        emit_br(b, new_block(b, n->name));
        start_dead(b, "goto.dead");
        return;

    case ND_CASE:
        build_stmt(b, n->lhs);
        return;

    default:
        build_expr(b, n);
        return;
    }
}

/* ---- function builder ---- */

static void build_func(struct ir_builder *b, struct node *n)
{
    struct ir_func *func;
    struct ir_block *entry;
    struct type *p;
    struct ir_val *av, *pv;
    int np;
    enum ir_type_kind pt;

    func = (struct ir_func *)ir_alloc(b, sizeof(struct ir_func));
    func->name = ir_strdup(b, n->name);
    func->ret_type = IRT_I32;
    func->attr_flags = n->attr_flags;
    func->is_static = n->is_static;
    if (n->ty && n->ty->ret) func->ret_type = type_to_irt(n->ty->ret);
    b->func = func; b->nlocals = 0;
    b->break_bb = NULL; b->continue_bb = NULL;

    entry = new_block(b, "entry");
    func->entry = entry; func->blocks = entry;
    func->nblocks = 1; b->cur_bb = entry;

    /* count params */
    np = 0;
    if (n->ty)
        for (p = n->ty->params; p; p = p->next) np++;
    func->nparams = np;
    if (np > 0)
        func->params = (struct ir_val **)ir_alloc(
            b, (usize)np * sizeof(struct ir_val *));

    /* emit param allocas */
    np = 0;
    if (n->ty) {
        for (p = n->ty->params; p; p = p->next) {
            pt = type_to_irt(p);
            pv = new_val(b, pt);
            func->params[np] = pv;
            if (p->name) {
                av = emit_alloca(b, p->size > 0 ? p->size : irt_size(pt));
                emit_store(b, pv, av);
                add_local(b, p->name, 0, av);
            }
            np++;
        }
    }

    build_stmt(b, n->body);

    if (!block_terminated(b)) {
        if (func->ret_type == IRT_VOID) emit_ret(b, NULL);
        else emit_ret(b, emit_const(b, func->ret_type, 0));
    }
}

/* ---- global variable builder ---- */

static struct ir_global *build_global(struct ir_builder *b, struct node *n)
{
    struct ir_global *g;
    g = (struct ir_global *)ir_alloc(b, sizeof(struct ir_global));
    g->name = ir_strdup(b, n->name);
    g->type = type_to_irt(n->ty);
    g->init_val = n->val;
    g->size = n->ty ? n->ty->size : 8;
    g->align = n->ty ? n->ty->align : 8;
    return g;
}

/* ---- public interface ---- */

struct ir_module *ir_build(struct node *ast, struct arena *a)
{
    struct ir_module *mod;
    struct ir_builder bld;
    struct node *n;
    struct ir_func *ft;
    struct ir_global *gt;
    struct ir_func *f;
    struct ir_global *g;

    memset(&bld, 0, sizeof(bld));
    bld.arena = a;
    mod = (struct ir_module *)arena_alloc(a, sizeof(struct ir_module));
    memset(mod, 0, sizeof(struct ir_module));
    mod->arena = a;
    ft = NULL; gt = NULL;

    for (n = ast; n; n = n->next) {
        if (n->kind == ND_FUNCDEF) {
            build_func(&bld, n);
            f = bld.func;
            if (!mod->funcs) mod->funcs = f; else ft->next = f;
            ft = f;
        } else if (n->kind == ND_VAR && n->offset == 0) {
            g = build_global(&bld, n);
            if (!mod->globals) mod->globals = g; else gt->next = g;
            gt = g;
        }
    }
    return mod;
}
