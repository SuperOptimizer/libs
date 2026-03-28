/*
 * cgen.c - Random C89 program generator for testing the free toolchain.
 * Generates valid, compilable, deterministic C89 programs to stdout.
 *
 * Usage: cgen <seed> [max_depth] [max_funcs] [max_stmts] [max_vars]
 *
 * Inspired by csmith/yarpgen but much simpler. Pure C89.
 * All variables declared at top of block. No // comments.
 *
 * Build:
 *   cc -std=c89 -pedantic -Wall -Wextra -o cgen cgen.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- configuration knobs (overridable via command line) ---- */

#ifndef DEFAULT_MAX_DEPTH
#define DEFAULT_MAX_DEPTH  4
#endif
#ifndef DEFAULT_MAX_FUNCS
#define DEFAULT_MAX_FUNCS  8
#endif
#ifndef DEFAULT_MAX_STMTS
#define DEFAULT_MAX_STMTS  15
#endif
#ifndef DEFAULT_MAX_VARS
#define DEFAULT_MAX_VARS   8
#endif

static int cfg_max_depth;
static int cfg_max_funcs;
static int cfg_max_stmts;
static int cfg_max_vars;

/* ---- simple PRNG (xorshift64) ---- */

static unsigned long rng_state;

static unsigned long rng_next(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return rng_state;
}

static int rng_int(int lo, int hi)
{
    unsigned long r;
    int range;

    if (lo >= hi) return lo;
    r = rng_next();
    range = hi - lo + 1;
    return lo + (int)(r % (unsigned long)range);
}

static int rng_bool(void)
{
    return rng_int(0, 1);
}

static int rng_percent(int pct)
{
    return rng_int(0, 99) < pct;
}

/* ---- output helpers ---- */

static int indent_level;

static void emit_indent(void)
{
    int i;
    for (i = 0; i < indent_level; i++) {
        printf("    ");
    }
}

static void emit(const char *s)
{
    printf("%s", s);
}

static void emit_int(int v)
{
    printf("%d", v);
}

static void emit_line(const char *s)
{
    emit_indent();
    printf("%s\n", s);
}

/* ---- type system ---- */

#define TYPE_INT       0
#define TYPE_CHAR      1
#define TYPE_LONG      2
#define TYPE_UINT      3
#define TYPE_UCHAR     4
#define TYPE_ULONG     5
#define TYPE_PTR_INT   6
#define TYPE_PTR_CHAR  7
#define TYPE_ARR_INT   8
#define TYPE_ARR_CHAR  9
#define TYPE_STRUCT    10
#define NUM_SIMPLE_TYPES 6

static int pick_simple_type(void)
{
    return rng_int(0, NUM_SIMPLE_TYPES - 1);
}

static void emit_type(int t)
{
    switch (t) {
    case TYPE_INT:       emit("int"); break;
    case TYPE_CHAR:      emit("char"); break;
    case TYPE_LONG:      emit("long"); break;
    case TYPE_UINT:      emit("unsigned int"); break;
    case TYPE_UCHAR:     emit("unsigned char"); break;
    case TYPE_ULONG:     emit("unsigned long"); break;
    case TYPE_PTR_INT:   emit("int *"); break;
    case TYPE_PTR_CHAR:  emit("char *"); break;
    case TYPE_ARR_INT:   emit("int"); break;
    case TYPE_ARR_CHAR:  emit("char"); break;
    default:             emit("int"); break;
    }
}

/* ---- variable tracking ---- */

#define MAX_SCOPE_VARS  64
#define MAX_SCOPES      32

struct var_info {
    char name[32];
    int type;
    int arr_size;      /* >0 if array */
    int is_ptr;        /* 1 if pointer type */
    int struct_idx;    /* index into structs[], -1 if not struct */
};

struct scope {
    struct var_info vars[MAX_SCOPE_VARS];
    int nvars;
};

static struct scope scopes[MAX_SCOPES];
static int scope_depth;

static void push_scope(void)
{
    if (scope_depth < MAX_SCOPES - 1) {
        scope_depth++;
        scopes[scope_depth].nvars = 0;
    }
}

static void pop_scope(void)
{
    if (scope_depth > 0) {
        scope_depth--;
    }
}

static struct var_info *add_var_ex(const char *name, int type,
                                   int arr_sz, int sidx)
{
    struct scope *s;
    struct var_info *v;

    s = &scopes[scope_depth];
    if (s->nvars >= MAX_SCOPE_VARS) return NULL;
    v = &s->vars[s->nvars++];
    strncpy(v->name, name, 31);
    v->name[31] = '\0';
    v->type = type;
    v->arr_size = arr_sz;
    v->is_ptr = (type == TYPE_PTR_INT || type == TYPE_PTR_CHAR);
    v->struct_idx = sidx;
    return v;
}

static struct var_info *add_var(const char *name, int type, int arr_sz)
{
    return add_var_ex(name, type, arr_sz, -1);
}

/* find a random variable visible in current scope (any type) */
static struct var_info *find_random_var(void)
{
    int d;
    int total;
    int pick;
    int count;

    total = 0;
    for (d = 0; d <= scope_depth; d++) {
        total += scopes[d].nvars;
    }
    if (total == 0) return NULL;

    pick = rng_int(0, total - 1);
    count = 0;
    for (d = 0; d <= scope_depth; d++) {
        if (count + scopes[d].nvars > pick) {
            return &scopes[d].vars[pick - count];
        }
        count += scopes[d].nvars;
    }
    return NULL;
}

/* find a random scalar (non-array, non-pointer, non-struct) variable */
static struct var_info *find_random_scalar(void)
{
    int attempts;
    struct var_info *v;

    for (attempts = 0; attempts < 20; attempts++) {
        v = find_random_var();
        if (v && v->arr_size == 0 && !v->is_ptr
            && v->type != TYPE_STRUCT) {
            return v;
        }
    }
    return NULL;
}

/* find a random array variable */
static struct var_info *find_random_array(void)
{
    int attempts;
    struct var_info *v;

    for (attempts = 0; attempts < 20; attempts++) {
        v = find_random_var();
        if (v && v->arr_size > 0) {
            return v;
        }
    }
    return NULL;
}

/* ---- function tracking ---- */

#define MAX_FUNCS_TOTAL 64

struct func_info {
    char name[32];
    int return_type;
    int nparams;
    int param_types[6];
};

static struct func_info funcs[MAX_FUNCS_TOTAL];
static int nfuncs;
static int current_func;

/* ---- struct tracking ---- */

#define MAX_STRUCTS 8
#define MAX_STRUCT_MEMBERS 6

struct struct_info {
    char name[32];
    int nmembers;
    char member_names[MAX_STRUCT_MEMBERS][16];
    int member_types[MAX_STRUCT_MEMBERS];
};

static struct struct_info structs[MAX_STRUCTS];
static int nstructs;

/* ---- global variable tracking ---- */

#define MAX_GLOBALS 32

static struct var_info globals[MAX_GLOBALS];
static int nglobals;

/* ---- forward declarations for recursive generation ---- */

static void gen_expr(int depth);
static void gen_stmt(int depth);
static void gen_block(int depth, int max_stmts);

/* ---- label generation for goto ---- */

static int label_counter;

static int new_label(void)
{
    return label_counter++;
}

/* ---- string literal pool ---- */

#define MAX_STRINGS 8

static const char *string_pool[] = {
    "hello",
    "world",
    "test",
    "fooX",
    "barY",
    "abcd",
    "abc123",
    "wxyz"
};

/* string lengths for safe indexing (excludes nul) */
static const int string_lens[] = {
    5, 5, 4, 4, 4, 4, 6, 4
};

/* ---- expression generation ---- */

static void gen_int_literal(void)
{
    int choice;

    choice = rng_int(0, 5);
    switch (choice) {
    case 0: emit_int(0); break;
    case 1: emit_int(1); break;
    case 2: emit_int(rng_int(2, 100)); break;
    case 3: emit_int(rng_int(-50, -1)); break;
    case 4: emit_int(rng_int(0, 255)); break;
    case 5: emit_int(rng_int(100, 10000)); break;
    }
}

static void gen_char_literal(void)
{
    int c;
    c = rng_int(32, 126);
    /* avoid problematic chars in char literal */
    if (c == '\'' || c == '\\') c = 'A';
    printf("'%c'", (char)c);
}

static int last_string_idx;

static void gen_string_literal(void)
{
    last_string_idx = rng_int(0, MAX_STRINGS - 1);
    printf("\"%s\"", string_pool[last_string_idx]);
}

static void gen_primary_expr(int depth)
{
    int choice;
    struct var_info *v;
    struct var_info *av;

    (void)depth;
    choice = rng_int(0, 10);

    switch (choice) {
    case 0: case 1: case 2:
        /* integer literal */
        gen_int_literal();
        break;
    case 3:
        /* char literal */
        gen_char_literal();
        break;
    case 4: case 5: case 6: case 7:
        /* variable reference */
        v = find_random_scalar();
        if (v) {
            emit(v->name);
        } else {
            gen_int_literal();
        }
        break;
    case 8:
        /* array indexing */
        av = find_random_array();
        if (av) {
            printf("%s[", av->name);
            printf("%d]", rng_int(0, av->arr_size - 1));
        } else {
            gen_int_literal();
        }
        break;
    case 9:
        /* sizeof */
        printf("(int)sizeof(");
        emit_type(pick_simple_type());
        emit(")");
        break;
    case 10:
        /* string literal indexing: "hello"[0] gives a char value */
        gen_string_literal();
        printf("[%d]", rng_int(0, string_lens[last_string_idx] - 1));
        break;
    }
}

static void gen_unary_expr(int depth)
{
    int op;

    if (depth <= 0 || rng_percent(60)) {
        gen_primary_expr(depth);
        return;
    }

    op = rng_int(0, 3);
    switch (op) {
    case 0:
        /* negation */
        emit("(-");
        gen_expr(depth - 1);
        emit(")");
        break;
    case 1:
        /* bitwise not */
        emit("(~");
        gen_expr(depth - 1);
        emit(")");
        break;
    case 2:
        /* logical not */
        emit("(!");
        gen_expr(depth - 1);
        emit(")");
        break;
    case 3:
        /* cast */
        emit("((");
        emit_type(pick_simple_type());
        emit(")(");
        gen_expr(depth - 1);
        emit("))");
        break;
    }
}

static void gen_binary_expr(int depth)
{
    int op;
    int is_div;

    if (depth <= 0) {
        gen_unary_expr(depth);
        return;
    }

    op = rng_int(0, 15);
    is_div = (op == 3 || op == 4);

    emit("(");
    gen_expr(depth - 1);

    switch (op) {
    case 0:  emit(" + "); break;
    case 1:  emit(" - "); break;
    case 2:  emit(" * "); break;
    case 3:  emit(" / "); break;   /* guarded below */
    case 4:  emit(" % "); break;   /* guarded below */
    case 5:  emit(" & "); break;
    case 6:  emit(" | "); break;
    case 7:  emit(" ^ "); break;
    case 8:  emit(" << "); break;
    case 9:  emit(" >> "); break;
    case 10: emit(" == "); break;
    case 11: emit(" != "); break;
    case 12: emit(" < "); break;
    case 13: emit(" > "); break;
    case 14: emit(" && "); break;
    case 15: emit(" || "); break;
    }

    if (is_div) {
        /* guard against division by zero */
        emit("(");
        gen_expr(depth - 1);
        emit(" | 1)");
    } else if (op == 8 || op == 9) {
        /* bounded shift: 0..30 */
        printf("(%d)", rng_int(0, 30));
    } else {
        gen_expr(depth - 1);
    }

    emit(")");
}

static void gen_ternary_expr(int depth)
{
    if (depth <= 0 || !rng_percent(20)) {
        gen_binary_expr(depth);
        return;
    }

    emit("(");
    gen_expr(depth - 1);
    emit(" ? ");
    gen_expr(depth - 1);
    emit(" : ");
    gen_expr(depth - 1);
    emit(")");
}

static void gen_func_call_expr(int depth)
{
    int fi;
    int i;

    /* only call functions with index strictly less than current_func
       to prevent self-recursion and mutual recursion.
       Low probability (15%) to avoid slow function-heavy programs. */
    if (nfuncs == 0 || depth <= 0 || current_func <= 0
        || !rng_percent(15)) {
        gen_ternary_expr(depth);
        return;
    }

    fi = rng_int(0, current_func - 1);
    if (fi >= nfuncs) fi = 0;

    printf("%s(", funcs[fi].name);
    for (i = 0; i < funcs[fi].nparams; i++) {
        if (i > 0) emit(", ");
        gen_expr(depth - 1);
    }
    emit(")");
}

static void gen_comma_expr(int depth)
{
    if (depth <= 0 || !rng_percent(10)) {
        gen_func_call_expr(depth);
        return;
    }

    emit("(");
    gen_expr(depth - 1);
    emit(", ");
    gen_expr(depth - 1);
    emit(")");
}

static void gen_expr(int depth)
{
    gen_comma_expr(depth);
}

/* ---- statement generation ---- */

static void gen_assign_stmt(int depth)
{
    struct var_info *v;
    int op;

    v = find_random_scalar();
    if (!v) {
        /* no variable available, just emit a bare expression */
        emit_indent();
        gen_expr(depth);
        emit(";\n");
        return;
    }

    emit_indent();
    emit(v->name);

    op = rng_int(0, 7);
    switch (op) {
    case 0: emit(" = "); break;
    case 1: emit(" += "); break;
    case 2: emit(" -= "); break;
    case 3: emit(" *= "); break;
    case 4:
        /* guarded /= */
        emit(" /= (");
        gen_expr(depth);
        emit(" | 1);\n");
        return;
    case 5:
        /* guarded %= */
        emit(" %= (");
        gen_expr(depth);
        emit(" | 1);\n");
        return;
    case 6: emit(" &= "); break;
    case 7: emit(" |= "); break;
    }

    gen_expr(depth);
    emit(";\n");
}

static void gen_array_assign_stmt(int depth)
{
    struct var_info *av;

    av = find_random_array();
    if (!av) {
        gen_assign_stmt(depth);
        return;
    }

    emit_indent();
    printf("%s[%d] = ", av->name, rng_int(0, av->arr_size - 1));
    gen_expr(depth);
    emit(";\n");
}

static void gen_if_stmt(int depth)
{
    emit_indent();
    emit("if (");
    gen_expr(depth - 1);
    emit(") {\n");

    indent_level++;
    gen_block(depth - 1, rng_int(1, 3));
    indent_level--;

    if (rng_bool()) {
        emit_line("} else {");
        indent_level++;
        gen_block(depth - 1, rng_int(1, 3));
        indent_level--;
    }

    emit_line("}");
}

static int loop_bound(int depth)
{
    /* scale iterations with depth to prevent exponential blowup:
       deeper nesting = fewer iterations */
    if (depth <= 1) return rng_int(1, 3);
    if (depth <= 2) return rng_int(1, 5);
    return rng_int(1, 10);
}

static void gen_while_stmt(int depth)
{
    char iter_var[32];
    int limit;

    /* use a bounded loop counter to avoid long execution */
    sprintf(iter_var, "_w%d", new_label());
    limit = loop_bound(depth);

    emit_indent();
    emit("{\n");
    indent_level++;

    emit_indent();
    printf("int %s;\n", iter_var);
    emit_indent();
    printf("%s = 0;\n", iter_var);
    emit_indent();
    printf("while (%s < %d) {\n", iter_var, limit);

    indent_level++;
    emit_indent();
    printf("%s++;\n", iter_var);
    gen_block(depth - 1, rng_int(1, 3));
    indent_level--;

    emit_indent();
    emit("}\n");
    indent_level--;
    emit_indent();
    emit("}\n");
}

static void gen_for_stmt(int depth)
{
    char iter_var[32];
    int limit;

    sprintf(iter_var, "_f%d", new_label());
    limit = loop_bound(depth);

    emit_indent();
    emit("{\n");
    indent_level++;

    emit_indent();
    printf("int %s;\n", iter_var);
    emit_indent();
    printf("for (%s = 0; %s < %d; %s++) {\n",
           iter_var, iter_var, limit, iter_var);

    indent_level++;
    gen_block(depth - 1, rng_int(1, 3));
    indent_level--;

    emit_indent();
    emit("}\n");
    indent_level--;
    emit_indent();
    emit("}\n");
}

static void gen_dowhile_stmt(int depth)
{
    char iter_var[32];
    int limit;

    sprintf(iter_var, "_d%d", new_label());
    limit = loop_bound(depth);

    emit_indent();
    emit("{\n");
    indent_level++;

    emit_indent();
    printf("int %s;\n", iter_var);
    emit_indent();
    printf("%s = 0;\n", iter_var);
    emit_indent();
    emit("do {\n");

    indent_level++;
    emit_indent();
    printf("%s++;\n", iter_var);
    gen_block(depth - 1, rng_int(1, 2));
    indent_level--;

    emit_indent();
    printf("} while (%s < %d);\n", iter_var, limit);
    indent_level--;
    emit_indent();
    emit("}\n");
}

static void gen_switch_stmt(int depth)
{
    int ncases;
    int i;
    int has_default;
    int case_vals[8];
    int cv;
    int dup;
    int j;

    ncases = rng_int(1, 5);
    if (ncases > 8) ncases = 8;
    has_default = rng_bool();

    /* pre-pick unique case values */
    for (i = 0; i < ncases; i++) {
        do {
            cv = i * 5 + rng_int(0, 4); /* spread to avoid collisions */
            dup = 0;
            for (j = 0; j < i; j++) {
                if (case_vals[j] == cv) { dup = 1; break; }
            }
        } while (dup);
        case_vals[i] = cv;
    }

    emit_indent();
    emit("switch (");
    gen_expr(depth - 1);
    emit(") {\n");

    for (i = 0; i < ncases; i++) {
        emit_indent();
        printf("case %d:\n", case_vals[i]);
        indent_level++;
        gen_block(depth - 1, rng_int(1, 2));
        emit_indent();
        emit("break;\n");
        indent_level--;
    }

    if (has_default) {
        emit_indent();
        emit("default:\n");
        indent_level++;
        gen_block(depth - 1, rng_int(1, 2));
        emit_indent();
        emit("break;\n");
        indent_level--;
    }

    emit_indent();
    emit("}\n");
}

static void gen_goto_stmt(void)
{
    int lbl;

    lbl = new_label();
    emit_indent();
    printf("goto _lbl%d;\n", lbl);
    emit_indent();
    printf("_lbl%d:\n", lbl);
}

static void gen_nested_block(int depth)
{
    emit_indent();
    emit("{\n");
    indent_level++;

    push_scope();
    gen_block(depth - 1, rng_int(1, 4));
    pop_scope();

    indent_level--;
    emit_indent();
    emit("}\n");
}

static void gen_struct_access_stmt(int depth)
{
    struct var_info *v;
    int si;
    int mi;
    int attempts;

    /* find a struct variable in scope with known struct index */
    for (attempts = 0; attempts < 20; attempts++) {
        v = find_random_var();
        if (v && v->type == TYPE_STRUCT && v->struct_idx >= 0
            && v->struct_idx < nstructs) {
            si = v->struct_idx;
            mi = rng_int(0, structs[si].nmembers - 1);
            emit_indent();
            printf("%s.%s = ", v->name, structs[si].member_names[mi]);
            gen_expr(depth);
            emit(";\n");
            return;
        }
    }

    /* fallback: regular assignment */
    gen_assign_stmt(depth);
}

static void gen_ptr_stmt(int depth)
{
    struct var_info *v;
    struct var_info *scalar;
    int attempts;

    /* find a pointer variable */
    for (attempts = 0; attempts < 20; attempts++) {
        v = find_random_var();
        if (v && v->is_ptr) {
            /* find a scalar to take address of */
            scalar = find_random_scalar();
            if (scalar) {
                emit_indent();
                printf("%s = &%s;\n", v->name, scalar->name);
                if (rng_bool()) {
                    emit_indent();
                    printf("*%s = ", v->name);
                    gen_expr(depth);
                    emit(";\n");
                }
                return;
            }
        }
    }

    gen_assign_stmt(depth);
}

static void gen_stmt(int depth)
{
    int choice;

    if (depth <= 0) {
        gen_assign_stmt(0);
        return;
    }

    /* at low depth, prefer simpler statements to avoid
       nested loops that run too long */
    if (depth <= 1) {
        choice = rng_int(0, 8);
    } else {
        choice = rng_int(0, 14);
    }

    switch (choice) {
    case 0: case 1: case 2: case 3:
        gen_assign_stmt(depth);
        break;
    case 4:
        gen_if_stmt(depth);
        break;
    case 5:
        if (depth >= 2) {
            gen_while_stmt(depth);
        } else {
            gen_assign_stmt(depth);
        }
        break;
    case 6:
        if (depth >= 2) {
            gen_for_stmt(depth);
        } else {
            gen_assign_stmt(depth);
        }
        break;
    case 7:
        if (depth >= 2) {
            gen_dowhile_stmt(depth);
        } else {
            gen_assign_stmt(depth);
        }
        break;
    case 8:
        gen_switch_stmt(depth);
        break;
    case 9:
        gen_goto_stmt();
        break;
    case 10:
        gen_nested_block(depth);
        break;
    case 11:
        gen_array_assign_stmt(depth);
        break;
    case 12:
        gen_struct_access_stmt(depth);
        break;
    case 13:
        gen_ptr_stmt(depth);
        break;
    case 14:
        /* bare expression statement */
        emit_indent();
        gen_expr(depth);
        emit(";\n");
        break;
    }
}

/* ---- declaration generation (two-phase for C89 compliance) ---- */

/*
 * Local variable plan: we pre-decide types and record them, then emit
 * all declarations first, then all initializations. This ensures C89
 * compliance (all decls at top of block before any statements).
 */

#define MAX_LOCAL_PLAN 64

struct local_plan {
    char name[32];
    int type;
    int arr_size;
    int struct_idx;   /* which struct type, if TYPE_STRUCT */
};

static struct local_plan local_plans[MAX_LOCAL_PLAN];
static int nlocal_plans;

/* Phase 1: decide what locals to create and add them to scope */
static void plan_local_var(const char *prefix, int idx)
{
    struct local_plan *lp;
    int type;

    if (nlocal_plans >= MAX_LOCAL_PLAN) return;
    lp = &local_plans[nlocal_plans++];

    sprintf(lp->name, "%s%d", prefix, idx);
    lp->arr_size = 0;
    lp->struct_idx = -1;

    type = rng_int(0, 11);

    if (type <= 5) {
        lp->type = pick_simple_type();
    } else if (type <= 7) {
        lp->arr_size = rng_int(2, 16);
        lp->type = rng_bool() ? TYPE_ARR_INT : TYPE_ARR_CHAR;
    } else if (type <= 9) {
        lp->type = rng_bool() ? TYPE_PTR_INT : TYPE_PTR_CHAR;
    } else if (type == 10 && nstructs > 0) {
        lp->type = TYPE_STRUCT;
        lp->struct_idx = rng_int(0, nstructs - 1);
    } else {
        lp->type = TYPE_INT;
    }

    add_var_ex(lp->name, lp->type, lp->arr_size, lp->struct_idx);
}

/* Phase 2a: emit all declarations */
static void emit_local_decls(void)
{
    int i;
    struct local_plan *lp;

    for (i = 0; i < nlocal_plans; i++) {
        lp = &local_plans[i];
        emit_indent();

        if (lp->type == TYPE_STRUCT && lp->struct_idx >= 0) {
            printf("struct %s %s;\n",
                   structs[lp->struct_idx].name, lp->name);
        } else if (lp->arr_size > 0) {
            emit_type(lp->type);
            printf(" %s[%d];\n", lp->name, lp->arr_size);
        } else {
            emit_type(lp->type);
            printf(" %s;\n", lp->name);
        }
    }
}

/* Phase 2b: emit all initializations (as statements) */
static void emit_local_inits(void)
{
    int i;
    int mi;
    struct local_plan *lp;

    for (i = 0; i < nlocal_plans; i++) {
        lp = &local_plans[i];

        if (lp->type == TYPE_STRUCT && lp->struct_idx >= 0) {
            for (mi = 0; mi < structs[lp->struct_idx].nmembers; mi++) {
                emit_indent();
                printf("%s.%s = ",
                       lp->name,
                       structs[lp->struct_idx].member_names[mi]);
                gen_int_literal();
                emit(";\n");
            }
        } else if (lp->arr_size > 0) {
            emit_indent();
            printf("%s[0] = ", lp->name);
            gen_int_literal();
            emit(";\n");
        } else if (lp->type == TYPE_PTR_INT
                   || lp->type == TYPE_PTR_CHAR) {
            emit_indent();
            printf("%s = 0;\n", lp->name);
        } else {
            emit_indent();
            printf("%s = ", lp->name);
            gen_int_literal();
            emit(";\n");
        }
    }
}

/* ---- block generation ---- */

static void gen_block(int depth, int max_stmts)
{
    int nstmts;
    int i;

    nstmts = rng_int(1, max_stmts);
    for (i = 0; i < nstmts; i++) {
        gen_stmt(depth);
    }
}

/* ---- struct generation ---- */

static void gen_struct_def(int idx)
{
    struct struct_info *s;
    int nm;
    int i;
    int mt;

    s = &structs[idx];
    sprintf(s->name, "S%d", idx);
    nm = rng_int(2, MAX_STRUCT_MEMBERS);
    s->nmembers = nm;

    printf("struct %s {\n", s->name);
    for (i = 0; i < nm; i++) {
        mt = pick_simple_type();
        s->member_types[i] = mt;
        sprintf(s->member_names[i], "m%d", i);
        printf("    ");
        emit_type(mt);
        printf(" m%d;\n", i);
    }
    printf("};\n\n");
}

/* ---- global variable generation ---- */

static void gen_global_var(int idx)
{
    int type;
    int arr_size;
    int sidx;
    struct var_info *g;

    type = rng_int(0, 7);
    sidx = -1;

    g = &globals[nglobals];

    if (type <= 4) {
        /* simple scalar */
        type = pick_simple_type();
        sprintf(g->name, "g%d", idx);
        g->type = type;
        g->arr_size = 0;
        g->is_ptr = 0;
        g->struct_idx = -1;

        emit_type(type);
        printf(" g%d", idx);

        /* optional initializer */
        if (rng_bool()) {
            emit(" = ");
            gen_int_literal();
        }
        emit(";\n");
    } else if (type <= 6) {
        /* array */
        arr_size = rng_int(2, 16);
        type = TYPE_ARR_INT;
        sprintf(g->name, "g%d", idx);
        g->type = type;
        g->arr_size = arr_size;
        g->is_ptr = 0;
        g->struct_idx = -1;

        printf("int g%d[%d];\n", idx, arr_size);
    } else {
        /* struct */
        if (nstructs > 0) {
            sidx = rng_int(0, nstructs - 1);
            sprintf(g->name, "gs%d", idx);
            g->type = TYPE_STRUCT;
            g->arr_size = 0;
            g->is_ptr = 0;
            g->struct_idx = sidx;

            printf("struct %s gs%d;\n", structs[sidx].name, idx);
        } else {
            sprintf(g->name, "g%d", idx);
            g->type = TYPE_INT;
            g->arr_size = 0;
            g->is_ptr = 0;
            g->struct_idx = -1;
            printf("int g%d;\n", idx);
        }
    }

    nglobals++;

    /* also add to scope 0 so functions can access */
    add_var_ex(g->name, g->type, g->arr_size, sidx);
}

/* ---- function generation ---- */

static void gen_function_decl(int idx)
{
    struct func_info *f;
    int np;
    int i;

    f = &funcs[idx];
    np = rng_int(0, 5);
    f->nparams = np;
    f->return_type = pick_simple_type();
    sprintf(f->name, "f%d", idx);

    for (i = 0; i < np; i++) {
        f->param_types[i] = pick_simple_type();
    }
}

static void gen_function_def(int idx)
{
    struct func_info *f;
    int nlocals;
    int max_locals;
    int nstmts;
    int i;
    struct var_info *ret_v;

    f = &funcs[idx];
    current_func = idx;

    /* emit function signature */
    emit_type(f->return_type);
    printf(" %s(", f->name);

    if (f->nparams == 0) {
        emit("void");
    } else {
        for (i = 0; i < f->nparams; i++) {
            if (i > 0) emit(", ");
            emit_type(f->param_types[i]);
            printf(" p%d", i);
        }
    }
    emit(")\n{\n");

    indent_level = 1;

    /* new scope for locals */
    push_scope();

    /* add params to scope */
    for (i = 0; i < f->nparams; i++) {
        char pname[32];
        sprintf(pname, "p%d", i);
        add_var(pname, f->param_types[i], 0);
    }

    /* plan local variables */
    nlocal_plans = 0;
    max_locals = rng_int(1, cfg_max_vars);
    nlocals = rng_int(1, max_locals);
    for (i = 0; i < nlocals; i++) {
        char prefix[16];
        sprintf(prefix, "v%d_", idx);
        plan_local_var(prefix, i);
    }

    /* C89: all declarations first, then statements */
    emit_local_decls();
    emit_local_inits();

    emit("\n");

    /* generate statements */
    nstmts = rng_int(1, cfg_max_stmts);
    gen_block(cfg_max_depth, nstmts);

    /* generate return statement */
    ret_v = find_random_scalar();
    emit_indent();
    emit("return ");
    if (ret_v) {
        printf("(%s)", ret_v->name);
    } else {
        gen_int_literal();
    }
    emit(";\n");

    pop_scope();

    emit("}\n\n");
    indent_level = 0;
}

/* ---- forward declaration generation ---- */

static void gen_forward_decls(int nf)
{
    int i;
    int j;
    struct func_info *f;

    for (i = 0; i < nf; i++) {
        f = &funcs[i];
        emit_type(f->return_type);
        printf(" %s(", f->name);
        if (f->nparams == 0) {
            emit("void");
        } else {
            for (j = 0; j < f->nparams; j++) {
                if (j > 0) emit(", ");
                emit_type(f->param_types[j]);
                printf(" p%d", j);
            }
        }
        emit(");\n");
    }
    emit("\n");
}

/* ---- main function generation ---- */

static void gen_main(void)
{
    int nlocals;
    int nstmts;
    int nfold;
    int ncalls;
    int fi;
    int i;
    int j;
    struct var_info *v;

    /* main can call all functions */
    current_func = nfuncs;

    printf("int main(void)\n{\n");

    indent_level = 1;

    push_scope();

    /* plan local variables (result + user vars) */
    nlocal_plans = 0;
    add_var("result", TYPE_INT, 0);

    nlocals = rng_int(1, cfg_max_vars);
    for (i = 0; i < nlocals; i++) {
        plan_local_var("m", i);
    }

    /* C89: emit all declarations first */
    emit_line("int result;");
    emit_local_decls();

    /* then all initializations */
    emit_indent();
    emit("result = 0;\n");
    emit_local_inits();

    emit("\n");

    /* generate body */
    nstmts = rng_int(3, cfg_max_stmts);
    gen_block(cfg_max_depth, nstmts);

    emit("\n");

    /* accumulate into result */
    emit_indent();
    emit("result = 0;\n");

    /* fold some variables into result */
    nfold = rng_int(1, 5);
    for (i = 0; i < nfold; i++) {
        v = find_random_scalar();
        if (v && strcmp(v->name, "result") != 0) {
            emit_indent();
            printf("result += (int)(%s);\n", v->name);
        }
    }

    /* optionally call some functions */
    if (nfuncs > 0) {
        ncalls = rng_int(1, 3);
        for (i = 0; i < ncalls; i++) {
            fi = rng_int(0, nfuncs - 1);
            emit_indent();
            printf("result += (int)%s(", funcs[fi].name);
            for (j = 0; j < funcs[fi].nparams; j++) {
                if (j > 0) emit(", ");
                gen_expr(2);
            }
            emit(");\n");
        }
    }

    emit("\n");

    /* return bounded exit code */
    emit_indent();
    emit("return (result >= 0 ? result : -result) & 255;\n");

    pop_scope();

    emit("}\n");
    indent_level = 0;
}

/* ---- top-level program generation ---- */

static void gen_program(unsigned long seed)
{
    int num_structs;
    int num_globals;
    int num_funcs;
    int i;

    /* reset all state */
    rng_state = seed;
    if (rng_state == 0) rng_state = 1;

    nfuncs = 0;
    nstructs = 0;
    nglobals = 0;
    scope_depth = 0;
    label_counter = 0;
    indent_level = 0;
    current_func = 0;
    memset(scopes, 0, sizeof(scopes));
    memset(funcs, 0, sizeof(funcs));
    memset(structs, 0, sizeof(structs));
    memset(globals, 0, sizeof(globals));

    /* initialize scope 0 for globals */
    scopes[0].nvars = 0;

    /* header comment */
    printf("/* Generated by cgen, seed=%lu */\n\n", seed);

    /* decide complexity */
    num_structs = rng_int(0, MAX_STRUCTS - 1);
    num_globals = rng_int(1, 10);
    num_funcs = rng_int(1, cfg_max_funcs);

    if (num_globals > MAX_GLOBALS) num_globals = MAX_GLOBALS;

    /* generate struct definitions */
    for (i = 0; i < num_structs; i++) {
        gen_struct_def(i);
        nstructs++;
    }

    /* generate global variables */
    for (i = 0; i < num_globals; i++) {
        gen_global_var(i);
    }
    if (num_globals > 0) emit("\n");

    /* prepare function signatures */
    for (i = 0; i < num_funcs; i++) {
        gen_function_decl(i);
        nfuncs++;
    }

    /* emit forward declarations */
    gen_forward_decls(num_funcs);

    /* generate function definitions */
    for (i = 0; i < num_funcs; i++) {
        gen_function_def(i);
    }

    /* generate main */
    gen_main();
}

/* ---- entry point ---- */

int main(int argc, char **argv)
{
    unsigned long seed;

    if (argc < 2) {
        fprintf(stderr,
            "Usage: cgen <seed> [max_depth] [max_funcs]"
            " [max_stmts] [max_vars]\n");
        return 1;
    }

    seed = (unsigned long)atol(argv[1]);

    cfg_max_depth = (argc > 2) ? atoi(argv[2]) : DEFAULT_MAX_DEPTH;
    cfg_max_funcs = (argc > 3) ? atoi(argv[3]) : DEFAULT_MAX_FUNCS;
    cfg_max_stmts = (argc > 4) ? atoi(argv[4]) : DEFAULT_MAX_STMTS;
    cfg_max_vars  = (argc > 5) ? atoi(argv[5]) : DEFAULT_MAX_VARS;

    /* clamp values */
    if (cfg_max_depth < 1) cfg_max_depth = 1;
    if (cfg_max_depth > 10) cfg_max_depth = 10;
    if (cfg_max_funcs < 1) cfg_max_funcs = 1;
    if (cfg_max_funcs > MAX_FUNCS_TOTAL) cfg_max_funcs = MAX_FUNCS_TOTAL;
    if (cfg_max_stmts < 1) cfg_max_stmts = 1;
    if (cfg_max_stmts > 100) cfg_max_stmts = 100;
    if (cfg_max_vars < 1) cfg_max_vars = 1;
    if (cfg_max_vars > MAX_SCOPE_VARS) cfg_max_vars = MAX_SCOPE_VARS;

    gen_program(seed);

    return 0;
}
