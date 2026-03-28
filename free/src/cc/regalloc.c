/*
 * regalloc.c - Linear scan register allocator for aarch64.
 * Maps IR virtual registers to physical registers, spilling to stack.
 * Algorithm: linear scan (Poletto & Sarkar, 1999), O(n log n).
 * Pure C89. No external dependencies beyond libc.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_VREGS     4096
#define MAX_INTERVALS 4096
#define MAX_ACTIVE    64
#define MAX_OPS       3
#define MAX_CALLS     512
#define NUM_CSAVED    15  /* x0-x7, x9-x15 */
#define NUM_EESAVED   10  /* x19-x28 */
#define NUM_AREGS     8   /* x0-x7 */
#define PHYS_NONE     (-1)

static const int csaved[15] = {9,10,11,12,13,14,15,0,1,2,3,4,5,6,7};
static const int eesaved[10] = {19,20,21,22,23,24,25,26,27,28};

enum ir_op {
    IR_NOP, IR_MOV, IR_MOVI, IR_ADD, IR_SUB, IR_MUL, IR_DIV, IR_MOD,
    IR_AND, IR_OR, IR_XOR, IR_SHL, IR_SHR, IR_NOT, IR_NEG,
    IR_CMP, IR_CSET, IR_LOAD, IR_STORE, IR_ADDR,
    IR_BR, IR_BRC, IR_CALL, IR_RET, IR_LABEL, IR_ARG, IR_PHI
};

struct ir_instr {
    enum ir_op op;
    int dst;              /* dest vreg, -1 if none */
    int src[MAX_OPS];     /* source vregs, -1 if unused */
    long imm;
    int label;            /* branch target / label id */
    char *name;           /* call target name */
    int nargs;
    int is_unsigned;
};

struct ir_func {
    char *name;
    struct ir_instr *instrs;
    int ninstr;
    int next_vreg;
    int nparams;
    int param_vregs[NUM_AREGS];
    int stack_size;
};

struct live_interval {
    int vreg;
    int start, end;       /* first/last use instruction index */
    int phys_reg;         /* physical reg or PHYS_NONE if spilled */
    int spill_slot;       /* stack offset if spilled, -1 otherwise */
    int is_fixed;
    int fixed_reg;
};

struct ra_state {
    struct live_interval iv[MAX_INTERVALS];
    int niv;
    int active[MAX_ACTIVE];
    int nactive;
    int reg_free[32];
    int ee_used[NUM_EESAVED]; /* callee-saved regs we touched */
    int nee;
    int spill_ends[MAX_VREGS]; /* end-point per spill slot */
    int nspill;
    int calls[MAX_CALLS];
    int ncalls;
};

static int align16(int v) { return (v + 15) & ~15; }
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

static int iv_cmp(const void *a, const void *b)
{
    const struct live_interval *ia = (const struct live_interval *)a;
    const struct live_interval *ib = (const struct live_interval *)b;
    return ia->start != ib->start ? ia->start - ib->start
                                  : ia->end - ib->end;
}

/* insertion sort for small active list, by end point */
static void sort_active(int *a, int n, struct live_interval *iv)
{
    int i, j, k, ke;
    for (i = 1; i < n; i++) {
        k = a[i]; ke = iv[k].end; j = i - 1;
        while (j >= 0 && iv[a[j]].end > ke) { a[j+1] = a[j]; j--; }
        a[j+1] = k;
    }
}

static void compute_intervals(struct ir_func *f, struct ra_state *s)
{
    int i, j, vreg, found;
    struct live_interval *p;

    s->niv = 0;
    for (i = 0; i < f->ninstr; i++) {
        struct ir_instr *ins = &f->instrs[i];
        int regs[MAX_OPS + 1];
        int nr = 0, r;

        if (ins->dst >= 0) regs[nr++] = ins->dst;
        for (j = 0; j < MAX_OPS; j++)
            if (ins->src[j] >= 0) regs[nr++] = ins->src[j];

        for (r = 0; r < nr; r++) {
            vreg = regs[r]; found = 0;
            for (j = 0; j < s->niv; j++) {
                if (s->iv[j].vreg == vreg) {
                    if (i < s->iv[j].start) s->iv[j].start = i;
                    if (i > s->iv[j].end) s->iv[j].end = i;
                    found = 1; break;
                }
            }
            if (!found && s->niv < MAX_INTERVALS) {
                p = &s->iv[s->niv];
                p->vreg = vreg; p->start = i; p->end = i;
                p->phys_reg = PHYS_NONE; p->spill_slot = -1;
                p->is_fixed = 0; p->fixed_reg = PHYS_NONE;
                s->niv++;
            }
        }
    }
}

static void assign_fixed(struct ir_func *f, struct ra_state *s)
{
    int i, j;
    for (i = 0; i < f->nparams && i < NUM_AREGS; i++) {
        for (j = 0; j < s->niv; j++) {
            if (s->iv[j].vreg == f->param_vregs[i]) {
                s->iv[j].is_fixed = 1;
                s->iv[j].fixed_reg = i;
                s->iv[j].phys_reg = i;
                break;
            }
        }
    }
}

static void coalesce(struct ir_func *f, struct ra_state *s)
{
    int i, j, si, di;
    struct ir_instr *ins;
    struct live_interval *sv, *dv;

    for (i = 0; i < f->ninstr; i++) {
        ins = &f->instrs[i];
        if (ins->op != IR_MOV || ins->dst < 0 || ins->src[0] < 0)
            continue;
        si = di = -1;
        for (j = 0; j < s->niv; j++) {
            if (s->iv[j].vreg == ins->src[0]) si = j;
            if (s->iv[j].vreg == ins->dst) di = j;
        }
        if (si < 0 || di < 0 || si == di) continue;
        sv = &s->iv[si]; dv = &s->iv[di];
        if (sv->is_fixed && dv->is_fixed && sv->fixed_reg != dv->fixed_reg)
            continue;
        /* overlap check: ok only if they meet exactly at the MOV */
        if (sv->start <= dv->end && dv->start <= sv->end)
            if (!(sv->end == i && dv->start == i)) continue;
        /* merge dst into src */
        if (dv->start < sv->start) sv->start = dv->start;
        if (dv->end > sv->end) sv->end = dv->end;
        if (dv->is_fixed && !sv->is_fixed) {
            sv->is_fixed = 1; sv->fixed_reg = dv->fixed_reg;
            sv->phys_reg = dv->phys_reg;
        }
        /* rewrite all references */
        for (j = 0; j < f->ninstr; j++) {
            int m;
            struct ir_instr *ri = &f->instrs[j];
            if (ri->dst == dv->vreg) ri->dst = sv->vreg;
            for (m = 0; m < MAX_OPS; m++)
                if (ri->src[m] == dv->vreg) ri->src[m] = sv->vreg;
        }
        ins->op = IR_NOP; ins->dst = -1; ins->src[0] = -1;
        /* remove merged interval */
        for (j = di; j < s->niv - 1; j++) s->iv[j] = s->iv[j+1];
        s->niv--;
    }
}

static int alloc_spill(struct ra_state *s, struct live_interval *iv)
{
    int i;
    for (i = 0; i < s->nspill; i++) {
        if (s->spill_ends[i] < iv->start) {
            s->spill_ends[i] = iv->end;
            return (i + 1) * 8;
        }
    }
    if (s->nspill >= MAX_VREGS) { fprintf(stderr,"regalloc: spill overflow\n"); exit(1); }
    i = s->nspill++;
    s->spill_ends[i] = iv->end;
    return (i + 1) * 8;
}

static void expire_old(struct ra_state *s, int pos)
{
    int j;
    while (s->nactive > 0) {
        struct live_interval *iv = &s->iv[s->active[0]];
        if (iv->end >= pos) break;
        s->reg_free[iv->phys_reg] = 1;
        for (j = 0; j < s->nactive - 1; j++) s->active[j] = s->active[j+1];
        s->nactive--;
    }
}

static void add_active(struct ra_state *s, int idx)
{
    if (s->nactive >= MAX_ACTIVE) { fprintf(stderr,"regalloc: active overflow\n"); exit(1); }
    s->active[s->nactive++] = idx;
    sort_active(s->active, s->nactive, s->iv);
}

static int try_alloc(struct ra_state *s)
{
    int i, r, k, dup;
    for (i = 0; i < NUM_CSAVED; i++) {
        r = csaved[i];
        if (s->reg_free[r]) { s->reg_free[r] = 0; return r; }
    }
    for (i = 0; i < NUM_EESAVED; i++) {
        r = eesaved[i];
        if (s->reg_free[r]) {
            s->reg_free[r] = 0;
            dup = 0;
            for (k = 0; k < s->nee; k++) if (s->ee_used[k] == r) { dup = 1; break; }
            if (!dup) s->ee_used[s->nee++] = r;
            return r;
        }
    }
    return PHYS_NONE;
}

static void spill_at(struct ra_state *s, int cur)
{
    int sp_idx;
    struct live_interval *sp_iv, *cur_iv;

    cur_iv = &s->iv[cur];
    sp_idx = s->active[s->nactive - 1];
    sp_iv = &s->iv[sp_idx];

    if (sp_iv->end > cur_iv->end) {
        cur_iv->phys_reg = sp_iv->phys_reg;
        sp_iv->phys_reg = PHYS_NONE;
        sp_iv->spill_slot = alloc_spill(s, sp_iv);
        s->nactive--;
        add_active(s, cur);
    } else {
        cur_iv->phys_reg = PHYS_NONE;
        cur_iv->spill_slot = alloc_spill(s, cur_iv);
    }
}

static void linear_scan(struct ra_state *s)
{
    int i, r, j, k;

    memset(s->reg_free, 0, sizeof(s->reg_free));
    for (i = 0; i < NUM_CSAVED; i++) s->reg_free[csaved[i]] = 1;
    for (i = 0; i < NUM_EESAVED; i++) s->reg_free[eesaved[i]] = 1;
    s->nactive = 0; s->nee = 0; s->nspill = 0;
    memset(s->spill_ends, 0, sizeof(s->spill_ends));

    qsort(s->iv, (size_t)s->niv, sizeof(struct live_interval), iv_cmp);

    for (i = 0; i < s->niv; i++) {
        expire_old(s, s->iv[i].start);
        if (s->iv[i].is_fixed) {
            r = s->iv[i].fixed_reg;
            if (!s->reg_free[r]) {
                for (j = 0; j < s->nactive; j++) {
                    if (s->iv[s->active[j]].phys_reg == r) {
                        s->iv[s->active[j]].phys_reg = PHYS_NONE;
                        s->iv[s->active[j]].spill_slot =
                            alloc_spill(s, &s->iv[s->active[j]]);
                        for (k = j; k < s->nactive-1; k++)
                            s->active[k] = s->active[k+1];
                        s->nactive--;
                        break;
                    }
                }
            }
            s->iv[i].phys_reg = r; s->reg_free[r] = 0;
            add_active(s, i);
        } else {
            r = try_alloc(s);
            if (r != PHYS_NONE) { s->iv[i].phys_reg = r; add_active(s, i); }
            else spill_at(s, i);
        }
    }
}

static int v2p(struct ra_state *s, int vreg)
{
    int i;
    for (i = 0; i < s->niv; i++) if (s->iv[i].vreg == vreg) return s->iv[i].phys_reg;
    return PHYS_NONE;
}
static int v2s(struct ra_state *s, int vreg)
{
    int i;
    for (i = 0; i < s->niv; i++) if (s->iv[i].vreg == vreg) return s->iv[i].spill_slot;
    return -1;
}

/* load vreg into phys; if already in reg, return that reg */
static int load_v(struct ra_state *s, int vreg, int scratch, FILE *o)
{
    int p, sp;
    if (vreg < 0) return scratch;
    p = v2p(s, vreg);
    if (p != PHYS_NONE) return p;
    sp = v2s(s, vreg);
    if (sp > 0) fprintf(o, "\tldr %s, [x29, #-%d]\n", xn(scratch), sp);
    return scratch;
}

/* store phys to spill slot if vreg is spilled */
static void store_v(struct ra_state *s, int vreg, int phys, FILE *o)
{
    int sp;
    if (vreg < 0) return;
    sp = v2s(s, vreg);
    if (sp > 0) fprintf(o, "\tstr %s, [x29, #-%d]\n", xn(phys), sp);
}

static int is_csaved(int r) { return r >= 0 && r <= 15 && r != 8; }
static int live_at(struct live_interval *iv, int p) { return p >= iv->start && p <= iv->end; }

/* caller save/restore around calls */
static void caller_save(struct ra_state *s, int pos, int base, FILE *o)
{
    int i;
    for (i = 0; i < s->niv; i++) {
        if (s->iv[i].phys_reg == PHYS_NONE || !is_csaved(s->iv[i].phys_reg)) continue;
        if (!live_at(&s->iv[i], pos)) continue;
        fprintf(o, "\tstr %s, [x29, #-%d]\n", xn(s->iv[i].phys_reg),
                base + s->iv[i].phys_reg * 8);
    }
}
static void caller_restore(struct ra_state *s, int pos, int base, FILE *o)
{
    int i;
    for (i = 0; i < s->niv; i++) {
        if (s->iv[i].phys_reg == PHYS_NONE || !is_csaved(s->iv[i].phys_reg)) continue;
        if (!live_at(&s->iv[i], pos)) continue;
        if (s->iv[i].phys_reg == 0) continue; /* x0 = return value */
        fprintf(o, "\tldr %s, [x29, #-%d]\n", xn(s->iv[i].phys_reg),
                base + s->iv[i].phys_reg * 8);
    }
}

static void emit_func(struct ir_func *f, struct ra_state *s, FILE *o)
{
    int i, fsz, sbase, dp, s0, s1;
    struct ir_instr *ins;

    fsz = align16(16 + f->stack_size + s->nspill * 8 + 128 + s->nee * 8);
    sbase = f->stack_size + s->nspill * 8 + 16;

    fprintf(o, "\n\t.global %s\n\t.type %s, %%function\n\t.p2align 2\n%s:\n",
            f->name, f->name, f->name);
    /* prologue */
    fprintf(o, "\tstp x29, x30, [sp, #-%d]!\n\tmov x29, sp\n", fsz);
    { int off = fsz - 16;
      for (i = 0; i < s->nee; i++) {
          fprintf(o, "\tstr %s, [x29, #-%d]\n", xn(s->ee_used[i]), off);
          off -= 8;
      }
    }

    for (i = 0; i < f->ninstr; i++) {
        ins = &f->instrs[i];
        switch (ins->op) {
        case IR_NOP: break;
        case IR_MOVI:
            dp = v2p(s, ins->dst); if (dp == PHYS_NONE) dp = 9;
            if (ins->imm >= -65536 && ins->imm <= 65535)
                fprintf(o, "\tmov %s, #%ld\n", xn(dp), ins->imm);
            else {
                unsigned long uv = (unsigned long)ins->imm;
                int sh, first = 1; unsigned long ch;
                for (sh = 0; sh < 64; sh += 16) {
                    ch = (uv >> sh) & 0xFFFFUL;
                    if (ch || (sh == 0 && first)) {
                        fprintf(o, "\t%s %s, #0x%lx, lsl #%d\n",
                                first ? "movz" : "movk", xn(dp), ch, sh);
                        first = 0;
                    }
                }
                if (first) fprintf(o, "\tmov %s, #0\n", xn(dp));
            }
            store_v(s, ins->dst, dp, o); break;

        case IR_MOV:
            s0 = load_v(s, ins->src[0], 10, o);
            dp = v2p(s, ins->dst); if (dp == PHYS_NONE) dp = s0;
            if (s0 != dp) fprintf(o, "\tmov %s, %s\n", xn(dp), xn(s0));
            store_v(s, ins->dst, dp, o); break;

        case IR_ADD: case IR_SUB: case IR_MUL:
        case IR_AND: case IR_OR: case IR_XOR:
        case IR_SHL: case IR_SHR:
            s0 = load_v(s, ins->src[0], 10, o);
            s1 = load_v(s, ins->src[1], 11, o);
            dp = v2p(s, ins->dst); if (dp == PHYS_NONE) dp = 9;
            { const char *m = "add";
              switch (ins->op) {
              case IR_ADD: m="add"; break; case IR_SUB: m="sub"; break;
              case IR_MUL: m="mul"; break; case IR_AND: m="and"; break;
              case IR_OR: m="orr"; break;  case IR_XOR: m="eor"; break;
              case IR_SHL: m="lsl"; break;
              case IR_SHR: m = ins->is_unsigned ? "lsr" : "asr"; break;
              default: break;
              }
              fprintf(o, "\t%s %s, %s, %s\n", m, xn(dp), xn(s0), xn(s1));
            }
            store_v(s, ins->dst, dp, o); break;

        case IR_DIV:
            s0 = load_v(s, ins->src[0], 10, o);
            s1 = load_v(s, ins->src[1], 11, o);
            dp = v2p(s, ins->dst); if (dp == PHYS_NONE) dp = 9;
            fprintf(o, "\t%s %s, %s, %s\n",
                    ins->is_unsigned ? "udiv" : "sdiv", xn(dp), xn(s0), xn(s1));
            store_v(s, ins->dst, dp, o); break;

        case IR_MOD:
            s0 = load_v(s, ins->src[0], 10, o);
            s1 = load_v(s, ins->src[1], 11, o);
            dp = v2p(s, ins->dst); if (dp == PHYS_NONE) dp = 9;
            fprintf(o, "\t%s x12, %s, %s\n",
                    ins->is_unsigned ? "udiv" : "sdiv", xn(s0), xn(s1));
            fprintf(o, "\tmsub %s, x12, %s, %s\n", xn(dp), xn(s1), xn(s0));
            store_v(s, ins->dst, dp, o); break;

        case IR_NOT:
            s0 = load_v(s, ins->src[0], 10, o);
            dp = v2p(s, ins->dst); if (dp == PHYS_NONE) dp = 9;
            fprintf(o, "\tmvn %s, %s\n", xn(dp), xn(s0));
            store_v(s, ins->dst, dp, o); break;

        case IR_NEG:
            s0 = load_v(s, ins->src[0], 10, o);
            dp = v2p(s, ins->dst); if (dp == PHYS_NONE) dp = 9;
            fprintf(o, "\tneg %s, %s\n", xn(dp), xn(s0));
            store_v(s, ins->dst, dp, o); break;

        case IR_CMP:
            s0 = load_v(s, ins->src[0], 10, o);
            s1 = load_v(s, ins->src[1], 11, o);
            fprintf(o, "\tcmp %s, %s\n", xn(s0), xn(s1)); break;

        case IR_CSET:
            dp = v2p(s, ins->dst); if (dp == PHYS_NONE) dp = 9;
            { const char *cc[] = {"eq","ne","lt","le","lo","ls"};
              int ci = (int)ins->imm;
              fprintf(o, "\tcset %s, %s\n", xn(dp),
                      (ci >= 0 && ci < 6) ? cc[ci] : "eq");
            }
            store_v(s, ins->dst, dp, o); break;

        case IR_LOAD:
            s0 = load_v(s, ins->src[0], 10, o);
            dp = v2p(s, ins->dst); if (dp == PHYS_NONE) dp = 9;
            fprintf(o, "\tldr %s, [%s]\n", xn(dp), xn(s0));
            store_v(s, ins->dst, dp, o); break;

        case IR_STORE:
            s0 = load_v(s, ins->src[0], 10, o);
            s1 = load_v(s, ins->src[1], 11, o);
            fprintf(o, "\tstr %s, [%s]\n", xn(s1), xn(s0)); break;

        case IR_ADDR:
            dp = v2p(s, ins->dst); if (dp == PHYS_NONE) dp = 9;
            if (ins->imm > 0)
                fprintf(o, "\tsub %s, x29, #%ld\n", xn(dp), ins->imm);
            else if (ins->name) {
                fprintf(o, "\tadrp %s, %s\n", xn(dp), ins->name);
                fprintf(o, "\tadd %s, %s, :lo12:%s\n", xn(dp), xn(dp), ins->name);
            }
            store_v(s, ins->dst, dp, o); break;

        case IR_BR:
            fprintf(o, "\tb .L.%d\n", ins->label); break;

        case IR_BRC:
            s0 = load_v(s, ins->src[0], 10, o);
            fprintf(o, "\tcmp %s, #0\n", xn(s0));
            fprintf(o, "\tb.%s .L.%d\n", ins->imm ? "ne" : "eq", ins->label);
            break;

        case IR_LABEL:
            fprintf(o, ".L.%d:\n", ins->label); break;

        case IR_CALL:
            caller_save(s, i, sbase, o);
            { int a;
              for (a = 0; a < ins->nargs && a < NUM_AREGS; a++) {
                  if (ins->src[a] >= 0) {
                      int ap = load_v(s, ins->src[a], 10, o);
                      if (ap != a) fprintf(o, "\tmov x%d, %s\n", a, xn(ap));
                  }
              }
            }
            fprintf(o, "\tbl %s\n", ins->name);
            dp = v2p(s, ins->dst);
            if (dp != PHYS_NONE && dp != 0)
                fprintf(o, "\tmov %s, x0\n", xn(dp));
            store_v(s, ins->dst, dp != PHYS_NONE ? dp : 0, o);
            caller_restore(s, i, sbase, o); break;

        case IR_RET:
            if (ins->src[0] >= 0) {
                s0 = load_v(s, ins->src[0], 0, o);
                if (s0 != 0) fprintf(o, "\tmov x0, %s\n", xn(s0));
            }
            fprintf(o, "\tb .L.return.%s\n", f->name); break;

        case IR_ARG:
            dp = v2p(s, ins->dst);
            if (dp != PHYS_NONE && dp != (int)ins->imm)
                fprintf(o, "\tmov %s, x%ld\n", xn(dp), ins->imm);
            store_v(s, ins->dst, dp != PHYS_NONE ? dp : (int)ins->imm, o);
            break;
        case IR_PHI: break;
        }
    }

    /* epilogue */
    fprintf(o, ".L.return.%s:\n", f->name);
    { int off = fsz - 16;
      for (i = 0; i < s->nee; i++) {
          fprintf(o, "\tldr %s, [x29, #-%d]\n", xn(s->ee_used[i]), off);
          off -= 8;
      }
    }
    fprintf(o, "\tldp x29, x30, [sp], #%d\n\tret\n", fsz);
    fprintf(o, "\t.size %s, .-%s\n", f->name, f->name);
}

/*
 * regalloc - run register allocation on an IR function.
 * 1. Compute live intervals   2. Pre-assign arg registers
 * 3. Coalesce MOV intervals   4. Linear scan allocation
 * 5. Emit code with physical registers + spill loads/stores
 */
void regalloc(struct ir_func *func, FILE *out)
{
    struct ra_state *s;
    int i;

    if (!func || !func->instrs || func->ninstr == 0) return;
    s = (struct ra_state *)calloc(1, sizeof(struct ra_state));
    if (!s) { fprintf(stderr, "regalloc: out of memory\n"); exit(1); }

    compute_intervals(func, s);
    assign_fixed(func, s);
    coalesce(func, s);
    linear_scan(s);

    /* find call sites */
    s->ncalls = 0;
    for (i = 0; i < func->ninstr; i++)
        if (func->instrs[i].op == IR_CALL && s->ncalls < MAX_CALLS)
            s->calls[s->ncalls++] = i;

    emit_func(func, s, out);
    free(s);
}
