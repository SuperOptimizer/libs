/*
 * gen.c - AArch64 code generator for the free C compiler.
 * Emits GAS-compatible assembly from an AST.
 * Stack-based expression evaluation: result always in x0.
 * Pure C89. No external dependencies.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "free.h"

/* pic.c */
extern int cc_pic_enabled;
extern void pic_emit_global_addr(FILE *out, const char *name);
extern void pic_emit_string_addr(FILE *out, int label_id);

extern int cc_debug_info;
extern int cc_function_sections;
extern int cc_data_sections;
extern int cc_general_regs_only;

/* dwarf.c */
extern void dwarf_init(void);
extern void dwarf_generate(struct node *prog, FILE *outfile,
                           const char *filename, const char *comp_dir);

/* ext_builtins.c */
extern int builtin_lookup(const char *name);
extern int builtin_is_atomic(int id);
extern void builtin_emit_atomic(FILE *out, int id, int memorder);

/* ext_asm.c */
struct asm_stmt;
extern void asm_emit(FILE *out, const struct asm_stmt *stmt,
                     const char *func_name);
extern void asm_emit_basic(FILE *out, const char *tmpl);
extern int asm_is_basic(const struct asm_stmt *stmt);

/* attribute flags from ext_attrs.c (must match ATTR_* values) */
#define GEN_ATTR_WEAK  (1 << 3)

/* forward declarations for HFA helpers */
static int is_hfa(struct type *ty);
static int hfa_member_count(struct type *ty);

static int string_literal_elem_size(const struct node *n)
{
    if (n == NULL || n->kind != ND_STR || n->ty == NULL ||
        n->ty->kind != TY_ARRAY || n->ty->base == NULL) {
        return 1;
    }
    if (n->ty->base->size <= 0) {
        return 1;
    }
    return n->ty->base->size;
}

static int string_literal_matches_type(struct type *ty, const struct node *n)
{
    if (ty == NULL || n == NULL || n->kind != ND_STR ||
        ty->kind != TY_ARRAY || ty->base == NULL) {
        return 0;
    }
    return ty->base->size == string_literal_elem_size(n);
}

/* ---- internal state ---- */
static FILE *out;
static int label_count;
static int depth;            /* stack depth tracking (in 8-byte slots) */
static char *current_func;   /* name of function being compiled */
static struct type *current_func_ret; /* return type of current function */
static int current_break_label;    /* target for break statements */
static int current_continue_label; /* target for continue statements */
static const char *gen_filename;   /* source file name for debug info */
static const char *gen_comp_dir;   /* compilation directory for debug info */

/* type system declarations */
extern struct type *ty_int;
extern struct type *ty_float;
extern struct type *ty_double;
int type_is_integer(struct type *ty);
int type_is_flonum(struct type *ty);
struct type *type_common(struct type *a, struct type *b);
struct type *type_common(struct type *a, struct type *b);
struct member *type_find_member(struct type *ty, const char *name);

/* FP literal label counter */
static int fp_literal_count;

/* variadic function state (set per-function for va_start/va_arg codegen) */
static int va_save_offset;
static int va_named_gp;
static int va_named_fp;
static int va_fp_save_offset;
static int va_stack_start;  /* byte offset from fp where variadic stack args begin */

/* leaf function optimization state */
static int current_is_leaf;      /* 1 if current function makes no calls */
static int current_stack_size;   /* stack frame size (for SP-relative addressing) */

/* large struct return: x8 holds the caller's buffer address (AAPCS64) */
static int ret_buf_offset;      /* fp offset where x8 is saved, 0 = not used */

/* flag: caller already set x8 for large struct return */
static int x8_preset;

/* ---- forward declarations ---- */
static void gen_expr(struct node *n);
static void gen_stmt(struct node *n);
static void gen_addr(struct node *n);
static int is_hfa(struct type *ty);
static int hfa_member_count(struct type *ty);
static struct node *unwrap_single_init_value(struct node *e);

/*
 * node_has_call - return 1 if the AST subtree contains any real
 * function call or a builtin that emits a branch (bl) instruction.
 */
static int node_has_call(struct node *n)
{
    struct node *c;

    for (; n != NULL; n = n->next) {
        if (n->kind == ND_CALL) {
            /* Builtins that are truly inlined (no bl) */
            if (n->name != NULL && (
                strcmp(n->name, "__builtin_expect") == 0 ||
                strcmp(n->name,
                    "__builtin_expect_with_probability") == 0 ||
                strcmp(n->name, "__builtin_frame_address") == 0 ||
                strcmp(n->name, "__builtin_return_address") == 0 ||
                strcmp(n->name, "__builtin_constant_p") == 0 ||
                strcmp(n->name, "__builtin_prefetch") == 0 ||
                strcmp(n->name, "__builtin_clz") == 0 ||
                strcmp(n->name, "__builtin_clzl") == 0 ||
                strcmp(n->name, "__builtin_clzll") == 0 ||
                strcmp(n->name, "__builtin_clrsb") == 0 ||
                strcmp(n->name, "__builtin_clrsbl") == 0 ||
                strcmp(n->name, "__builtin_clrsbll") == 0 ||
                strcmp(n->name, "__builtin_ctz") == 0 ||
                strcmp(n->name, "__builtin_ctzl") == 0 ||
                strcmp(n->name, "__builtin_ctzll") == 0 ||
                strcmp(n->name, "__builtin_popcount") == 0 ||
                strcmp(n->name, "__builtin_popcountl") == 0 ||
                strcmp(n->name, "__builtin_popcountll") == 0 ||
                strcmp(n->name, "__builtin_bswap16") == 0 ||
                strcmp(n->name, "__builtin_bswap32") == 0 ||
                strcmp(n->name, "__builtin_bswap64") == 0 ||
                strcmp(n->name, "__builtin_ffs") == 0 ||
                strcmp(n->name, "__builtin_ffsl") == 0 ||
                strcmp(n->name, "__builtin_ffsll") == 0 ||
                strcmp(n->name, "__builtin_trap") == 0 ||
                strcmp(n->name, "__builtin_unreachable") == 0 ||
                strcmp(n->name, "__builtin_offsetof") == 0 ||
                strcmp(n->name, "__builtin_types_compatible_p") == 0 ||
                strcmp(n->name, "__builtin_abs") == 0 ||
                strcmp(n->name, "abs") == 0 ||
                strcmp(n->name, "__builtin_labs") == 0 ||
                strcmp(n->name, "__builtin_llabs") == 0 ||
                strcmp(n->name, "llabs") == 0 ||
                strcmp(n->name, "__builtin_fabs") == 0 ||
                strcmp(n->name, "__builtin_fabsf") == 0 ||
                strcmp(n->name, "__builtin_sqrt") == 0 ||
                strcmp(n->name, "__builtin_sqrtf") == 0 ||
                strcmp(n->name, "__builtin_signbit") == 0 ||
                strcmp(n->name, "__builtin_signbitf") == 0 ||
                strcmp(n->name, "__builtin_copysign") == 0 ||
                strcmp(n->name, "__builtin_copysignf") == 0 ||
                strcmp(n->name, "__builtin_conj") == 0 ||
                strcmp(n->name, "__builtin_conjf") == 0 ||
                strcmp(n->name, "__builtin_conjl") == 0 ||
                strcmp(n->name, "__builtin_isnan") == 0 ||
                strcmp(n->name, "__builtin_isnanf") == 0 ||
                strcmp(n->name, "__builtin_isinf") == 0 ||
                strcmp(n->name, "__builtin_isinff") == 0 ||
                strcmp(n->name, "__builtin_isinfl") == 0 ||
                strcmp(n->name, "__builtin_isfinite") == 0 ||
                strcmp(n->name, "__builtin_inf") == 0 ||
                strcmp(n->name, "__builtin_inff") == 0 ||
                strcmp(n->name, "__builtin_nan") == 0 ||
                strcmp(n->name, "__builtin_nanf") == 0 ||
                strcmp(n->name, "__builtin_huge_val") == 0 ||
                strcmp(n->name, "__builtin_huge_valf") == 0 ||
                strcmp(n->name, "__builtin_add_overflow_p") == 0 ||
                strcmp(n->name, "__builtin_sub_overflow_p") == 0 ||
                strcmp(n->name, "__builtin_mul_overflow_p") == 0 ||
                strncmp(n->name, "__atomic_", 9) == 0 ||
                strncmp(n->name, "__sync_", 7) == 0)) {
                /* these are inlined, don't count as calls */
            } else {
                return 1;
            }
        }
        /* Inline asm with operands needs x29 (frame pointer) for
         * load/store of operands, so treat as non-leaf. */
        if (n->kind == ND_GCC_ASM && n->asm_data &&
            !asm_is_basic(n->asm_data))
            return 1;
        if (node_has_call(n->lhs)) return 1;
        if (node_has_call(n->rhs)) return 1;
        if (node_has_call(n->cond)) return 1;
        if (node_has_call(n->then_)) return 1;
        if (node_has_call(n->els)) return 1;
        if (node_has_call(n->init)) return 1;
        if (node_has_call(n->inc)) return 1;
        if (node_has_call(n->args)) return 1;
        for (c = n->body; c; c = c->next) {
            if (node_has_call(c)) return 1;
        }
    }
    return 0;
}

/*
 * node_has_dynamic_stack - return 1 if the AST subtree contains a
 * compound literal or init list that allocates stack space at runtime.
 * Such constructs need pre-allocated frame space so they don't interfere
 * with the push-stack mechanism.
 */
static int node_has_dynamic_stack(struct node *n)
{
    struct node *c;

    for (; n != NULL; n = n->next) {
        if (n->kind == ND_COMPOUND_LIT || n->kind == ND_INIT_LIST)
            return 1;
        if (node_has_dynamic_stack(n->lhs)) return 1;
        if (node_has_dynamic_stack(n->rhs)) return 1;
        if (node_has_dynamic_stack(n->cond)) return 1;
        if (node_has_dynamic_stack(n->then_)) return 1;
        if (node_has_dynamic_stack(n->els)) return 1;
        if (node_has_dynamic_stack(n->init)) return 1;
        if (node_has_dynamic_stack(n->inc)) return 1;
        if (node_has_dynamic_stack(n->args)) return 1;
        for (c = n->body; c; c = c->next) {
            if (node_has_dynamic_stack(c)) return 1;
        }
    }
    return 0;
}

/*
 * assign_compound_lit_offsets - walk the AST and assign frame offsets
 * to compound literals and init lists so they use pre-allocated stack
 * space instead of dynamically adjusting sp.  *offset is the running
 * total (positive, measured from fp downward).  Each compound literal
 * gets its own slot; the same slot can be reused by compound literals
 * in separate statements since they don't overlap in lifetime.
 * Within a single expression tree, compound literals must NOT overlap,
 * so we always advance the offset.
 */
static void assign_compound_lit_offsets(struct node *n, int *offset)
{
    struct node *c;

    for (; n != NULL; n = n->next) {
        if (n->kind == ND_COMPOUND_LIT || n->kind == ND_INIT_LIST) {
            int sz;
            sz = (n->ty != NULL) ? n->ty->size : 8;
            sz = (sz + 15) & ~15; /* align to 16 */
            *offset += sz;
            n->offset = *offset;  /* store frame offset */
        }
        assign_compound_lit_offsets(n->lhs, offset);
        assign_compound_lit_offsets(n->rhs, offset);
        assign_compound_lit_offsets(n->cond, offset);
        assign_compound_lit_offsets(n->then_, offset);
        assign_compound_lit_offsets(n->els, offset);
        assign_compound_lit_offsets(n->init, offset);
        assign_compound_lit_offsets(n->inc, offset);
        assign_compound_lit_offsets(n->args, offset);
        for (c = n->body; c; c = c->next) {
            assign_compound_lit_offsets(c, offset);
        }
    }
}

/*
 * is_32bit - return 1 if the type fits in a 32-bit register.
 */
static int is_32bit(struct type *ty)
{
    if (ty == NULL) return 0;
    if (ty->size > 4) return 0;
    if (ty->kind == TY_PTR || ty->kind == TY_ARRAY ||
        ty->kind == TY_FUNC) return 0;
    if (ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE) return 0;
    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) return 0;
    return 1;
}

/* ---- output helpers ---- */

static void emit(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(out, "\t");
    vfprintf(out, fmt, ap);
    fprintf(out, "\n");
    va_end(ap);
}

static void emit_label(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    fprintf(out, ":\n");
    va_end(ap);
}

static void emit_comment(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(out, "\t/* ");
    vfprintf(out, fmt, ap);
    fprintf(out, " */\n");
    va_end(ap);
}

static int new_label(void)
{
    return label_count++;
}

/* ---- stack management ---- */

static void push(void)
{
    emit("str x0, [sp, #-16]!");
    depth++;
}

static void pop(const char *reg)
{
    emit("ldr %s, [sp], #16", reg);
    depth--;
}

static void push_fp(void)
{
    emit("str d0, [sp, #-16]!");
    depth++;
}

static void pop_fp(const char *reg)
{
    emit("ldr %s, [sp], #16", reg);
    depth--;
}

static void push_q(void)
{
    emit("sub sp, sp, #16");
    emit("str q0, [sp]");
    depth++;
}

static void pop_q(const char *reg)
{
    emit("ldr %s, [sp]", reg);
    emit("add sp, sp, #16");
    depth--;
}

/* ---- type helpers ---- */

/*
 * align_to - round up val to the next multiple of a.
 * a must be a power of two.
 */
static int align_to(int val, int a)
{
    return (val + a - 1) & ~(a - 1);
}

/*
 * type_is_aggregate - return 1 for aggregate object types.
 */
static int type_is_aggregate(struct type *ty)
{
    if (ty == NULL) return 0;
    return ty->kind == TY_STRUCT || ty->kind == TY_UNION ||
           ty->kind == TY_ARRAY;
}

/* Treat any 16-byte non-integer scalar as fp128 long double.
 * This avoids depending on a specific TY_LDOUBLE enum value. */
static int type_is_fp128(struct type *ty)
{
    if (ty == NULL) {
        return 0;
    }
    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION ||
        ty->kind == TY_ARRAY || ty->kind == TY_FUNC) {
        return 0;
    }
    if (ty->kind == TY_COMPLEX_FLOAT || ty->kind == TY_COMPLEX_DOUBLE) {
        return 0;
    }
    if (type_is_integer(ty)) {
        return 0;
    }
    return ty->size == 16;
}

static int type_is_real_flonum(struct type *ty)
{
    if (ty == NULL) {
        return 0;
    }
    return ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE;
}

static int type_is_fp_scalar(struct type *ty)
{
    return type_is_real_flonum(ty) || type_is_fp128(ty);
}

/* ---- fp128 conversion helpers (AAPCS64 / libgcc) ---- */

/* Convert integer value in x0/w0 to fp128 result in q0 (v0).
 * Uses libgcc helpers matching GCC on native aarch64. */
static void emit_int_to_fp128(struct type *src_ty)
{
    int sz;
    int is_uns;

    sz = (src_ty != NULL) ? src_ty->size : 8;
    is_uns = (src_ty != NULL) ? src_ty->is_unsigned : 0;

    if (sz <= 4) {
        if (is_uns) {
            emit("bl __floatunsitf");
        } else {
            emit("bl __floatsitf");
        }
    } else {
        if (is_uns) {
            emit("bl __floatunditf");
        } else {
            emit("bl __floatditf");
        }
    }
}

/* Convert double value in d0 to fp128 result in q0 (v0). */
static void emit_double_to_fp128(void)
{
    emit("bl __extenddftf2");
}

/* Convert fp128 value in q0 (v0) to double result in d0. */
static void emit_fp128_to_double(void)
{
    emit("bl __trunctfdf2");
}

/* Convert fp128 value in q0 (v0) to float result in s0, then
 * promote to our d0 convention (exact). */
static void emit_fp128_to_float_in_d0(void)
{
    emit_fp128_to_double();
    emit("fcvt s0, d0");
    emit("fcvt d0, s0");
}

/* Set x0 to 0/1 for truthiness of fp128 in q0. Clobbers v1 and w0. */
static void emit_fp128_is_nonzero(void)
{
    emit("movi v1.16b, #0"); /* q1 = 0.0 */
    emit("bl __netf2");      /* w0 != 0 iff q0 != q1 */
    emit("cmp w0, #0");
    emit("cset x0, ne");
}

/* Set x0 to 0/1 for fp128 infinity checks in q0. */
static void emit_fp128_isinf(void)
{
    push_q();
    emit("ldr x10, [sp, #8]");
    emit("and x10, x10, #0x7fffffffffffffff");
    emit("mov x11, #0");
    emit("movk x11, #0x7fff, lsl #48");
    emit("cmp x10, x11");
    emit("cset w0, eq");
    emit("add sp, sp, #16");
    depth--;
}

/* Negate fp128 value in q0 by flipping its sign bit. */
static void emit_fp128_negate(void)
{
    push_q();
    emit("ldr x10, [sp, #8]");
    emit("mov x11, #0x8000000000000000");
    emit("eor x10, x10, x11");
    emit("str x10, [sp, #8]");
    pop_q("q0");
}

/* Convert current scalar value into fp128 in q0 from source type. */
static void emit_scalar_to_fp128(struct type *src_ty)
{
    if (src_ty != NULL && type_is_fp128(src_ty)) {
        return;
    }
    if (src_ty != NULL && type_is_real_flonum(src_ty)) {
        emit_double_to_fp128();
        return;
    }
    emit_int_to_fp128(src_ty);
}

/* Normalize the current expression value into x0 = 0/1 for truth tests. */
static void emit_truth_test(struct node *n)
{
    if (n != NULL && n->ty != NULL) {
        if (type_is_fp128(n->ty)) {
            emit_fp128_is_nonzero();
            return;
        }
        if (n->ty->kind == TY_COMPLEX_DOUBLE) {
            emit("ldr d0, [x0]");
            emit("ldr d1, [x0, #8]");
            emit("fcmp d0, #0.0");
            emit("cset x0, ne");
            emit("fcmp d1, #0.0");
            emit("cset x1, ne");
            emit("orr x0, x0, x1");
            return;
        }
        if (n->ty->kind == TY_COMPLEX_FLOAT) {
            emit("ldr s0, [x0]");
            emit("ldr s1, [x0, #4]");
            emit("fcmp s0, #0.0");
            emit("cset x0, ne");
            emit("fcmp s1, #0.0");
            emit("cset x1, ne");
            emit("orr x0, x0, x1");
            return;
        }
        if (type_is_real_flonum(n->ty)) {
            emit("fcmp d0, #0.0");
            emit("cset x0, ne");
            return;
        }
    }
    emit("cmp x0, #0");
    emit("cset x0, ne");
}

/* ---- load/store by type size ---- */

/*
 * emit_load - load value from address in x0, result in x0.
 * Size determines the load instruction used.
 */
static void emit_load(struct type *ty)
{
    int sz;

    if (ty == NULL) {
        emit("ldr x0, [x0]");
        return;
    }

    sz = ty->size;

    if (ty->kind == TY_ARRAY || ty->kind == TY_STRUCT ||
        ty->kind == TY_UNION || ty->kind == TY_FUNC) {
        /* arrays/functions decay to pointer; structs/unions stay as address */
        return;
    }

    /* complex types: treat as aggregate (address stays in x0) */
    if (ty->kind == TY_COMPLEX_FLOAT || ty->kind == TY_COMPLEX_DOUBLE) {
        return;
    }

    /* floating-point load: address in x0, result in d0 (or q0) */
    if (ty->kind == TY_FLOAT) {
        emit("ldr s0, [x0]");
        emit("fcvt d0, s0");
        return;
    }
    if (ty->kind == TY_DOUBLE) {
        emit("ldr d0, [x0]");
        return;
    }
    if (type_is_fp128(ty)) {
        /* long double (IEEE binary128): load into q0 */
        emit("ldr q0, [x0]");
        return;
    }

    if (sz == 1) {
        if (ty->is_unsigned) {
            emit("ldrb w0, [x0]");
        } else {
            emit("ldrsb x0, [x0]");
        }
    } else if (sz == 2) {
        if (ty->is_unsigned) {
            emit("ldrh w0, [x0]");
        } else {
            emit("ldrsh x0, [x0]");
        }
    } else if (sz == 4) {
        if (ty->is_unsigned) {
            emit("ldr w0, [x0]");
        } else {
            emit("ldrsw x0, [x0]");
        }
    } else {
        emit("ldr x0, [x0]");
    }
}

/*
 * emit_bitfield_store - store a bitfield value using read-modify-write.
 * x0 = new value, x1 = address of storage unit.
 * Uses BFI to insert only the relevant bits.
 */
static void emit_bitfield_store(struct node *n)
{
    int bw, bo;
    unsigned long mask;

    bw = n->bit_width;
    bo = n->bit_offset;
    if (bw == 0) {
        return; /* zero-width bitfield: nothing to store */
    }
    if (bw >= 64) {
        mask = ~0UL;
    } else {
        mask = (1UL << bw) - 1;
    }
    emit("and x2, x0, #0x%lx", mask);
    if (bo + bw <= 32) {
        emit("ldr w3, [x1]");
        emit("bfi w3, w2, #%d, #%d", bo, bw);
        emit("str w3, [x1]");
    } else {
        emit("ldr x3, [x1]");
        emit("bfi x3, x2, #%d, #%d", bo, bw);
        emit("str x3, [x1]");
    }
    emit("mov x0, x2");
}

/*
 * emit_bitfield_load - load and extract a bitfield value.
 * Address of storage unit must be in x0.
 * Result is in x0 (extracted and optionally sign-extended).
 */
static void emit_bitfield_load(struct node *n)
{
    int bw, bo;
    unsigned long mask;

    bw = n->bit_width;
    bo = n->bit_offset;

    if (bo + bw <= 32) {
        emit("ldr w0, [x0]");
    } else {
        emit("ldr x0, [x0]");
    }
    if (bo > 0) {
        emit("lsr x0, x0, #%d", bo);
    }
    if (bw < 64) {
        mask = (1UL << bw) - 1;
        emit("and x0, x0, #0x%lx", mask);
    }
    /* sign extension for signed bitfields */
    if (n->ty && !n->ty->is_unsigned &&
        n->ty->kind != TY_ENUM && bw < 64) {
        int shift_amt;
        shift_amt = 64 - bw;
        emit("lsl x0, x0, #%d", shift_amt);
        emit("asr x0, x0, #%d", shift_amt);
    }
}

/*
 * emit_bitfield_truncate_width - truncate x0 to a bitfield width and
 * sign-extend if the target type is signed.
 */
static void emit_bitfield_truncate_width(int bw, struct type *ty)
{
    unsigned long mask;
    int shift_amt;

    if (bw <= 0 || bw >= 64) {
        return;
    }

    mask = (1UL << bw) - 1;
    emit("and x0, x0, #0x%lx", mask);
    if (ty != NULL && !ty->is_unsigned && ty->kind != TY_ENUM) {
        shift_amt = 64 - bw;
        emit("lsl x0, x0, #%d", shift_amt);
        emit("asr x0, x0, #%d", shift_amt);
    }
}

/*
 * emit_bitfield_result_mask - truncate arithmetic results when either
 * direct operand is a bitfield member.
 */
static void emit_bitfield_result_mask(struct node *n)
{
    int bw;

    if (n == NULL) {
        return;
    }

    bw = 0;
    if (n->lhs != NULL && n->lhs->kind == ND_MEMBER &&
        n->lhs->bit_width > 0) {
        bw = n->lhs->bit_width;
    }
    if (n->rhs != NULL && n->rhs->kind == ND_MEMBER &&
        n->rhs->bit_width > bw) {
        bw = n->rhs->bit_width;
    }
    emit_bitfield_truncate_width(bw, n->ty);
}

/*
 * emit_store - store value from x0 to address in x1.
 * Size determines the store instruction used.
 */
static void emit_store(struct type *ty)
{
    int sz;
    int i;

    if (ty == NULL) {
        emit("str x0, [x1]");
        return;
    }

    sz = ty->size;

    /* _Bool store: convert nonzero to 1 */
    if (ty->kind == TY_BOOL) {
        emit("cmp x0, #0");
        emit("cset x0, ne");
        emit("strb w0, [x1]");
        return;
    }

    /* floating-point store: value in d0, address in x1 */
    if (ty->kind == TY_FLOAT) {
        emit("fcvt s0, d0");
        emit("str s0, [x1]");
        return;
    }
    if (ty->kind == TY_DOUBLE) {
        emit("str d0, [x1]");
        return;
    }
    if (type_is_fp128(ty)) {
        emit("str q0, [x1]");
        return;
    }

    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION ||
        ty->kind == TY_ARRAY ||
        ty->kind == TY_COMPLEX_FLOAT || ty->kind == TY_COMPLEX_DOUBLE) {
        /* copy aggregate: x0=src, x1=dst, copy sz bytes */
        for (i = 0; i + 8 <= sz; i += 8) {
            emit("ldr x2, [x0, #%d]", i);
            emit("str x2, [x1, #%d]", i);
        }
        for (; i + 4 <= sz; i += 4) {
            emit("ldr w2, [x0, #%d]", i);
            emit("str w2, [x1, #%d]", i);
        }
        for (; i < sz; i++) {
            emit("ldrb w2, [x0, #%d]", i);
            emit("strb w2, [x1, #%d]", i);
        }
        return;
    }

    if (sz == 1) {
        emit("strb w0, [x1]");
    } else if (sz == 2) {
        emit("strh w0, [x1]");
    } else if (sz == 4) {
        emit("str w0, [x1]");
    } else {
        emit("str x0, [x1]");
    }
}

/* ---- constant loading ---- */

/*
 * emit_num - load an immediate value into x0.
 * Uses movz/movn and movk sequences for values that don't fit in 16 bits.
 */
static void emit_num(long val)
{
    unsigned long uval;
    unsigned long chunk;
    int shift;
    int first;

    if (val >= 0 && val <= 65535) {
        emit("mov x0, #%ld", val);
        return;
    }

    if (val < 0 && val >= -65536) {
        /* small negative: assembler handles mov with negative imm */
        emit("mov x0, #%ld", val);
        return;
    }

    /* general case: movz + movk sequence */
    uval = (unsigned long)val;

    first = 1;
    for (shift = 0; shift < 64; shift += 16) {
        chunk = (uval >> shift) & 0xFFFFUL;
        if (chunk != 0 || (shift == 0 && first)) {
            if (first) {
                emit("movz x0, #0x%lx, lsl #%d", chunk, shift);
                first = 0;
            } else {
                emit("movk x0, #0x%lx, lsl #%d", chunk, shift);
            }
        }
    }

    /* if val is 0 but we somehow got here */
    if (first) {
        emit("mov x0, #0");
    }
}

/*
 * emit_load_imm_reg - load an immediate value into an arbitrary register.
 * Uses mov for small values, movz/movk for larger ones.
 */
static void emit_load_imm_reg(const char *reg, long val)
{
    unsigned long uval;
    unsigned long chunk;
    int shift;
    int first;
    int max_shift;

    if (val >= 0 && val <= 65535) {
        emit("mov %s, #%ld", reg, val);
        return;
    }

    if (val < 0 && val >= -65536) {
        emit("mov %s, #%ld", reg, val);
        return;
    }

    /* general case: movz + movk sequence.
     * w-registers only support lsl #0 and #16. */
    uval = (unsigned long)val;
    max_shift = (reg[0] == 'w') ? 32 : 64;

    first = 1;
    for (shift = 0; shift < max_shift; shift += 16) {
        chunk = (uval >> shift) & 0xFFFFUL;
        if (chunk != 0 || (shift == 0 && first)) {
            if (first) {
                emit("movz %s, #0x%lx, lsl #%d", reg, chunk, shift);
                first = 0;
            } else {
                emit("movk %s, #0x%lx, lsl #%d", reg, chunk, shift);
            }
        }
    }

    if (first) {
        emit("mov %s, #0", reg);
    }
}

/*
 * emit_sub_fp - emit: sub xDST, x29, #OFFSET
 * Handles offsets > 4095 by using an intermediate register.
 */
static void emit_sub_fp(const char *dst, int offset)
{
    if (offset <= 4095) {
        emit("sub %s, x29, #%d", dst, offset);
    } else if (offset <= 65535) {
        emit("mov %s, #%d", dst, offset);
        emit("sub %s, x29, %s", dst, dst);
    } else {
        emit("movz %s, #%d", dst, offset & 0xffff);
        emit("movk %s, #%d, lsl #16", dst,
             (offset >> 16) & 0xffff);
        emit("sub %s, x29, %s", dst, dst);
    }
}

/*
 * emit_store_gp_value_to_local - store a GP register value into a local slot
 * exactly, without writing past the object for odd-sized structs.
 */
static void emit_store_gp_value_to_local(int reg, int size, int offset)
{
    int bi;

    emit_sub_fp("x10", offset);
    if (size == 1) {
        emit("strb w%d, [x10]", reg);
    } else if (size == 2) {
        emit("strh w%d, [x10]", reg);
    } else if (size == 4) {
        emit("str w%d, [x10]", reg);
    } else if (size == 8) {
        emit("str x%d, [x10]", reg);
    } else {
        emit("mov x11, x%d", reg);
        for (bi = 0; bi < size; bi++) {
            if (bi > 0) {
                emit("lsr x11, x11, #8");
            }
            emit("strb w11, [x10, #%d]", bi);
        }
    }
}

/* ---- address generation ---- */

/*
 * gen_addr - compute the address of a node and leave it in x0.
 * Used for lvalues and the address-of operator.
 */
static void gen_addr(struct node *n)
{
    switch (n->kind) {
    case ND_VAR:
        if (n->offset != 0) {
            if (n->is_upvar) {
                /* upvar: access through static link in x19 */
                emit_comment("addr of upvar '%s' [x19, #-%d]",
                             n->name ? n->name : "?", n->offset);
                if (n->offset <= 4095) {
                    emit("sub x0, x19, #%d", n->offset);
                } else if (n->offset <= 65535) {
                    emit("mov x0, #%d", n->offset);
                    emit("sub x0, x19, x0");
                } else {
                    emit_load_imm_reg("x0", n->offset);
                    emit("sub x0, x19, x0");
                }
            } else if (current_is_leaf) {
                /* Leaf: SP-relative. sp_off = stack + depth*16 - offset */
                int sp_off;
                sp_off = current_stack_size + depth * 16 - n->offset;
                emit_comment("addr of local '%s' [sp, #%d]",
                             n->name ? n->name : "?", sp_off);
                if (sp_off >= 0 && sp_off <= 4095) {
                    emit("add x0, sp, #%d", sp_off);
                } else {
                    emit_load_imm_reg("x0", sp_off);
                    emit("add x0, sp, x0");
                }
            } else {
                /* local variable: address is fp - offset */
                emit_comment("addr of local '%s' [fp, #-%d]",
                             n->name ? n->name : "?", n->offset);
                if (n->offset <= 4095) {
                    emit("sub x0, x29, #%d", n->offset);
                } else if (n->offset <= 65535) {
                    emit("mov x0, #%d", n->offset);
                    emit("sub x0, x29, x0");
                } else {
                    emit("movz x0, #%d", n->offset & 0xffff);
                    emit("movk x0, #%d, lsl #16",
                         (n->offset >> 16) & 0xffff);
                    emit("sub x0, x29, x0");
                }
            }
        } else if (cc_pic_enabled ||
                   (n->attr_flags & GEN_ATTR_WEAK)) {
            /* PIC or weak symbol: load address via GOT
             * (weak undefined symbols must go through GOT so the
             *  linker can resolve them to NULL) */
            emit_comment("addr of global '%s' (GOT)",
                         n->name ? n->name : "?");
            pic_emit_global_addr(out, n->name);
        } else {
            /* global variable: use adrp + add */
            emit_comment("addr of global '%s'",
                         n->name ? n->name : "?");
            emit("adrp x0, %s", n->name);
            emit("add x0, x0, :lo12:%s", n->name);
        }
        return;

    case ND_DEREF:
        /* address of *p is just p */
        gen_expr(n->lhs);
        return;

    case ND_MEMBER:
        gen_addr(n->lhs);
        if (n->offset > 0 && n->offset <= 4095) {
            emit("add x0, x0, #%d", n->offset);
        } else if (n->offset > 4095 && n->offset <= 65535) {
            emit("mov x1, #%d", n->offset);
            emit("add x0, x0, x1");
        } else if (n->offset > 65535) {
            emit("movz x1, #%d", n->offset & 0xffff);
            emit("movk x1, #%d, lsl #16",
                 (n->offset >> 16) & 0xffff);
            emit("add x0, x0, x1");
        }
        return;

    case ND_COMPOUND_LIT:
        /* compound literal is an lvalue; run gen_expr then restore
         * the address (gen_expr may load for scalar types). */
        if (n->offset > 0) {
            /* Ensure the compound literal is initialized first. */
            gen_expr(n);
            /* Re-compute the address (gen_expr may have loaded
             * the value for scalar types). */
            if (current_is_leaf) {
                int sp_off;
                sp_off = current_stack_size + depth * 16 - n->offset;
                emit("add x0, sp, #%d", sp_off);
            } else {
                emit("sub x0, x29, #%d", n->offset);
            }
        } else {
            gen_expr(n);
        }
        return;

    case ND_CALL:
        /* function call returning a struct/union -- evaluate the call,
         * spill the result to a stack temp, and return its address.
         * This handles cases like fr_hfa12().a */
        {
            int sz;
            int hfa_k;
            gen_expr(n);
            sz = n->ty ? n->ty->size : 8;
            hfa_k = (n->ty != NULL) ? is_hfa(n->ty) : 0;
            if (n->ty != NULL &&
                n->ty->kind == TY_COMPLEX_DOUBLE) {
                /* complex double: d0=real, d1=imag, spill */
                emit("sub sp, sp, #16");
                depth++;
                emit("str d0, [sp]");
                emit("str d1, [sp, #8]");
                emit("mov x0, sp");
            } else if (n->ty != NULL &&
                       n->ty->kind == TY_COMPLEX_FLOAT) {
                /* complex float: s0=real, s1=imag, spill */
                emit("sub sp, sp, #16");
                depth++;
                emit("str s0, [sp]");
                emit("str s1, [sp, #4]");
                emit("mov x0, sp");
            } else if (hfa_k) {
                /* HFA: return values in d0-d3 or s0-s3,
                 * spill to stack temp */
                int hcnt, mi, msz;
                int alloc_sz;
                hcnt = hfa_member_count(n->ty);
                msz = hfa_k;
                alloc_sz = (sz + 15) & ~15;
                emit("sub sp, sp, #%d", alloc_sz);
                depth += alloc_sz / 16;
                for (mi = 0; mi < hcnt; mi++) {
                    if (hfa_k == 4) {
                        emit("str s%d, [sp, #%d]",
                             mi, mi * msz);
                    } else if (hfa_k == 16) {
                        emit("str q%d, [sp, #%d]",
                             mi, mi * msz);
                    } else {
                        emit("str d%d, [sp, #%d]",
                             mi, mi * msz);
                    }
                }
                emit("mov x0, sp");
            } else if (sz <= 8) {
                /* small struct: result in x0, push it */
                push();
                emit("mov x0, sp");
            } else if (sz <= 16) {
                /* 9-16 byte struct: result in x0/x1 */
                emit("stp x0, x1, [sp, #-16]!");
                depth++;
                emit("mov x0, sp");
            } else {
                /* large struct: x0 already points to result
                 * (hidden return pointer), just use it */
            }
        }
        return;

    case ND_STMT_EXPR:
        /* statement expression as lvalue: evaluate it, spill to
         * stack temp, return its address.
         * This handles cases like ({ struct s t; t; }).member */
        {
            int sz;
            gen_expr(n);
            sz = n->ty ? n->ty->size : 8;
            if (sz <= 8) {
                push();
                emit("mov x0, sp");
            } else if (sz <= 16) {
                emit("stp x0, x1, [sp, #-16]!");
                depth++;
                emit("mov x0, sp");
            } else {
                /* value already on stack via pointer */
            }
        }
        return;

    case ND_COMMA_EXPR:
        /* comma expression as lvalue: the last operand is the lvalue.
         * Evaluate the lhs for side effects, then gen_addr of rhs. */
        gen_expr(n->lhs);
        gen_addr(n->rhs);
        return;

    case ND_ASSIGN:
    case ND_TERNARY:
    case ND_CAST:
        /* These produce rvalues; spill to stack temp for lvalue use.
         * Handles patterns like (x=y).member, (cond?x:y).member */
        {
            gen_expr(n);
            /* For struct/union types, gen_expr already returns the
             * address in x0 (emit_load is a no-op for aggregates).
             * Return it directly as the lvalue address. */
            if (n->ty != NULL &&
                (n->ty->kind == TY_STRUCT || n->ty->kind == TY_UNION)) {
                /* x0 is already the struct address */
            } else {
                /* Scalar: gen_expr returned the value. Spill to
                 * stack temp and return its address. */
                int sz;
                sz = n->ty ? n->ty->size : 8;
                if (sz <= 8) {
                    push();
                    emit("mov x0, sp");
                } else if (sz <= 16) {
                    emit("stp x0, x1, [sp, #-16]!");
                    depth++;
                    emit("mov x0, sp");
                } else {
                    /* large: pointer already in x0 */
                }
            }
        }
        return;

    default:
        fprintf(stderr, "gen_addr: not an lvalue (kind=%d)\n",
                n->kind);
        exit(1);
    }
}

/*
 * inc_dec_step - return the increment/decrement step size.
 * For pointer types, step by the pointed-to size; otherwise 1.
 */
static int inc_dec_step(struct node *operand)
{
    if (operand->ty != NULL &&
        (operand->ty->kind == TY_PTR || operand->ty->kind == TY_ARRAY) &&
        operand->ty->base != NULL) {
        return operand->ty->base->size;
    }
    return 1;
}

/* ---- expression generation ---- */

static const char *arg_regs[8] = {
    "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"
};


/*
 * get_func_type - extract the TY_FUNC type from a call node.
 * Returns NULL if the function type cannot be determined.
 */
static struct type *get_func_type(struct node *call)
{
    struct type *ty;
    if (call->lhs == NULL) return NULL;
    ty = call->lhs->ty;
    if (ty == NULL) return NULL;
    if (ty->kind == TY_FUNC) return ty;
    if (ty->kind == TY_PTR && ty->base && ty->base->kind == TY_FUNC)
        return ty->base;
    return NULL;
}

/*
 * is_hfa - check if a struct/union is a Homogeneous Floating-point
 * Aggregate (HFA). An HFA has 1-4 members, all float, all double,
 * or all long double.
 * Returns the base member size (4, 8, 16) or 0 if not HFA.
 */
static int hfa_count_recursive(struct type *ty, int *base_sz);

static int is_hfa(struct type *ty)
{
    int base_sz;
    int count;

    if (ty == NULL) return 0;
    if (ty->kind != TY_STRUCT && ty->kind != TY_UNION) return 0;
    if (ty->members == NULL) return 0;

    base_sz = 0;
    count = hfa_count_recursive(ty, &base_sz);
    if (count < 1 || count > 4) return 0;
    return base_sz;
}

/*
 * hfa_count_recursive - count floating members recursively.
 * Sets *base_sz to 4/8/16 on first encounter.
 * Returns total count, or 0 if not an HFA.
 */
static int hfa_count_recursive(struct type *ty, int *base_sz)
{
    struct member *m;
    int count;

    if (ty == NULL || ty->members == NULL) return 0;

    count = 0;
    for (m = ty->members; m != NULL; m = m->next) {
        int leaf_sz;

        if (m->ty == NULL) return 0;

        leaf_sz = 0;
        if (m->ty->kind == TY_FLOAT) {
            leaf_sz = 4;
        } else if (m->ty->kind == TY_DOUBLE) {
            leaf_sz = 8;
        } else if (type_is_fp128(m->ty)) {
            leaf_sz = 16;
        }

        if (leaf_sz != 0) {
            if (*base_sz == 0) {
                *base_sz = leaf_sz;
            } else if (leaf_sz != *base_sz) {
                return 0;
            }
            count++;
        } else if (m->ty->kind == TY_STRUCT ||
                   m->ty->kind == TY_UNION) {
            int sub;
            sub = hfa_count_recursive(m->ty, base_sz);
            if (sub == 0) return 0;
            count += sub;
        } else {
            return 0;
        }
    }
    return count;
}

/*
 * hfa_member_count - count leaf float/double members in an HFA.
 */
static int hfa_member_count(struct type *ty)
{
    int base_sz;
    base_sz = 0;
    return hfa_count_recursive(ty, &base_sz);
}

/*
 * emit_il_store - store a scalar value from x0 (or d0) to the address
 * at [sp] + off.  Used by init-list assignment where base addr is on stack.
 */
static void emit_il_store(struct type *mty, int off,
                          struct node *val)
{
    emit("ldr x1, [sp]");
    if (string_literal_matches_type(mty, val)) {
        int arr_sz;
        int elem_sz;
        int str_len;
        int ci4;

        arr_sz = mty->size;
        elem_sz = string_literal_elem_size(val);
        str_len = (int)val->val + elem_sz;
        if (str_len > arr_sz) {
            str_len = arr_sz;
        }
        for (ci4 = 0; ci4 < str_len; ci4++) {
            emit("ldrb w11, [x0, #%d]", ci4);
            emit("strb w11, [x1, #%d]", off + ci4);
        }
        for (; ci4 < arr_sz; ci4++) {
            emit("strb wzr, [x1, #%d]", off + ci4);
        }
        return;
    }
    if (mty && (mty->kind == TY_STRUCT || mty->kind == TY_UNION ||
                mty->kind == TY_ARRAY)) {
        /* struct/union/array: x0 is source address, copy to x1+off */
        int ci3, sz3;
        sz3 = mty->size;
        emit("mov x10, x0");
        for (ci3 = 0; ci3 + 8 <= sz3; ci3 += 8) {
            emit("ldr x11, [x10, #%d]", ci3);
            emit("str x11, [x1, #%d]", off + ci3);
        }
        for (; ci3 + 4 <= sz3; ci3 += 4) {
            emit("ldr w11, [x10, #%d]", ci3);
            emit("str w11, [x1, #%d]", off + ci3);
        }
        for (; ci3 < sz3; ci3++) {
            emit("ldrb w11, [x10, #%d]", ci3);
            emit("strb w11, [x1, #%d]", off + ci3);
        }
        return;
    }
    if (mty && type_is_fp128(mty)) {
        if (val && val->ty && !type_is_fp128(val->ty)) {
            if (val->ty != NULL && type_is_flonum(val->ty)) {
                /* d0 already holds a double value */
                emit_double_to_fp128();
            } else if (val->ty != NULL) {
                emit_int_to_fp128(val->ty);
            }
        }
        emit("str q0, [x1, #%d]", off);
    } else if (mty && type_is_flonum(mty)) {
        if (val && val->ty && type_is_fp128(val->ty)) {
            if (mty->kind == TY_FLOAT) {
                emit_fp128_to_float_in_d0();
            } else {
                emit_fp128_to_double();
            }
        } else if (val && val->ty && !type_is_flonum(val->ty)) {
            if (val->ty->is_unsigned && val->ty->size <= 4)
                emit("ucvtf d0, w0");
            else if (val->ty->size <= 4)
                emit("scvtf d0, w0");
            else if (val->ty->is_unsigned)
                emit("ucvtf d0, x0");
            else
                emit("scvtf d0, x0");
        }
        if (mty->kind == TY_FLOAT) {
            emit("fcvt s0, d0");
            emit("str s0, [x1, #%d]", off);
        } else {
            emit("str d0, [x1, #%d]", off);
        }
    } else if (mty && mty->size == 1) {
        emit("strb w0, [x1, #%d]", off);
    } else if (mty && mty->size == 2) {
        emit("strh w0, [x1, #%d]", off);
    } else if (mty && mty->size == 4) {
        emit("str w0, [x1, #%d]", off);
    } else {
        emit("str x0, [x1, #%d]", off);
    }
}

/*
 * emit_il_init_struct_r - recursively initialize an object from a
 * flattened init-list cursor. Returns the next unconsumed element.
 */
static struct node *emit_il_init_struct_r(struct node *elem,
                                          struct type *ty,
                                          int base_off)
{
    struct node *cur;
    struct node *val;

    if (elem == NULL || ty == NULL) {
        return elem;
    }

    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        struct member *mbr;

        mbr = ty->members;
        cur = elem;
        while (cur != NULL && mbr != NULL) {
            int off;

            off = base_off + mbr->offset;
            val = (cur->kind == ND_DESIG_INIT) ? cur->lhs : cur;
            if (val != NULL) {
                if (val->kind == ND_INIT_LIST &&
                    (mbr->ty == NULL ||
                     !type_is_aggregate(mbr->ty))) {
                    val = unwrap_single_init_value(val);
                }
                if (mbr->bit_width > 0) {
                    int bw, bo;
                    unsigned long mask;

                    bw = mbr->bit_width;
                    bo = mbr->bit_offset;
                    mask = (bw >= 64) ? ~0UL : (1UL << bw) - 1;
                    gen_expr(val);
                    emit("ldr x1, [sp]");
                    if (bw < 64) {
                        emit("and x2, x0, #0x%lx", mask);
                    } else {
                        emit("mov x2, x0");
                    }
                    if (bo + bw <= 32) {
                        emit("ldr w3, [x1, #%d]", off);
                        emit("bfi w3, w2, #%d, #%d", bo, bw);
                        emit("str w3, [x1, #%d]", off);
                    } else {
                        emit("ldr x3, [x1, #%d]", off);
                        emit("bfi x3, x2, #%d, #%d", bo, bw);
                        emit("str x3, [x1, #%d]", off);
                    }
                    cur = cur->next;
                    mbr = mbr->next;
                    if (ty->kind == TY_UNION) break;
                    continue;
                }

                if (mbr->ty != NULL && type_is_aggregate(mbr->ty)) {
                    if (val->kind == ND_INIT_LIST) {
                        emit_il_init_struct_r(val->body, mbr->ty, off);
                        cur = cur->next;
                    } else {
                        cur = emit_il_init_struct_r(cur, mbr->ty, off);
                    }
                    mbr = mbr->next;
                    if (ty->kind == TY_UNION) break;
                    continue;
                }

                if (val->kind == ND_INIT_LIST) {
                    gen_expr(val);
                    emit_il_store(mbr->ty, off, val);
                } else {
                    gen_expr(val);
                    emit_il_store(mbr->ty, off, val);
                }
            }
            cur = cur->next;
            mbr = mbr->next;
            if (ty->kind == TY_UNION) break;
        }
        return cur;
    }

    if (ty->kind == TY_ARRAY && ty->base != NULL) {
        int idx;
        int esz;

        esz = ty->base->size;
        idx = 0;
        cur = elem;
        while (cur != NULL && idx < ty->array_len) {
            int off;

            off = base_off + idx * esz;
            val = (cur->kind == ND_DESIG_INIT) ? cur->lhs : cur;
            if (val != NULL) {
                if (val->kind == ND_INIT_LIST &&
                    (ty->base == NULL ||
                     !type_is_aggregate(ty->base))) {
                    val = unwrap_single_init_value(val);
                }
                if (string_literal_matches_type(ty, val)) {
                    gen_expr(val);
                    emit_il_store(ty, off, val);
                    return cur->next;
                }

                if (type_is_aggregate(ty->base)) {
                    if (val->kind == ND_INIT_LIST) {
                        emit_il_init_struct_r(val->body, ty->base, off);
                        cur = cur->next;
                    } else {
                        cur = emit_il_init_struct_r(cur, ty->base, off);
                    }
                    idx++;
                    continue;
                }

                gen_expr(val);
                emit_il_store(ty->base, off, val);
            }
            cur = cur->next;
            idx++;
        }
        return cur;
    }

    val = (elem->kind == ND_DESIG_INIT) ? elem->lhs : elem;
    if (val != NULL) {
        if (val->kind == ND_INIT_LIST &&
            (ty == NULL || !type_is_aggregate(ty))) {
            val = unwrap_single_init_value(val);
        }
        gen_expr(val);
        emit_il_store(ty, base_off, val);
    }
    return elem->next;
}

/*
 * emit_il_init_struct - recursively initialize a struct/union/array
 * for init-list assignment.  The base address is at [sp].
 * base_off is the byte offset from that base address.
 */
static void emit_il_init_struct(struct node *init, struct type *ty,
                                int base_off)
{
    if (init == NULL || init->kind != ND_INIT_LIST || ty == NULL)
        return;
    (void)emit_il_init_struct_r(init->body, ty, base_off);
}

/*
 * emit_cl_store - store a scalar value from x0 (or d0 for floats) into
 * the compound literal buffer at x9 + off, based on the member type.
 */
static void emit_cl_store(struct type *mty, int off, struct node *val)
{
    if (string_literal_matches_type(mty, val)) {
        int arr_sz;
        int elem_sz;
        int str_len;
        int ci;

        arr_sz = mty->size;
        elem_sz = string_literal_elem_size(val);
        str_len = (int)val->val + elem_sz;
        if (str_len > arr_sz) {
            str_len = arr_sz;
        }
        for (ci = 0; ci < str_len; ci++) {
            emit("ldrb w11, [x0, #%d]", ci);
            emit("strb w11, [x9, #%d]", off + ci);
        }
        for (; ci < arr_sz; ci++) {
            emit("strb wzr, [x9, #%d]", off + ci);
        }
        return;
    }

    if (mty && (mty->kind == TY_STRUCT || mty->kind == TY_UNION ||
                mty->kind == TY_ARRAY)) {
        /* struct/union/array: x0 is source address, copy to x9+off */
        int ci3, sz3;
        sz3 = mty->size;
        emit("mov x10, x0");
        for (ci3 = 0; ci3 + 8 <= sz3; ci3 += 8) {
            emit("ldr x11, [x10, #%d]", ci3);
            emit("str x11, [x9, #%d]", off + ci3);
        }
        for (; ci3 + 4 <= sz3; ci3 += 4) {
            emit("ldr w11, [x10, #%d]", ci3);
            emit("str w11, [x9, #%d]", off + ci3);
        }
        for (; ci3 < sz3; ci3++) {
            emit("ldrb w11, [x10, #%d]", ci3);
            emit("strb w11, [x9, #%d]", off + ci3);
        }
        return;
    }

    if (mty && type_is_fp128(mty)) {
        if (val && val->ty && !type_is_fp128(val->ty)) {
            if (type_is_flonum(val->ty)) {
                emit_double_to_fp128();
            } else {
                emit_int_to_fp128(val->ty);
            }
        }
        emit("str q0, [x9, #%d]", off);
    } else if (mty && type_is_flonum(mty)) {
        if (val && val->ty && type_is_fp128(val->ty)) {
            if (mty->kind == TY_FLOAT) {
                emit_fp128_to_float_in_d0();
            } else {
                emit_fp128_to_double();
            }
        } else if (val && val->ty && !type_is_flonum(val->ty)) {
            /* integer -> float/double */
            if (val->ty->is_unsigned && val->ty->size <= 4)
                emit("ucvtf d0, w0");
            else if (val->ty->size <= 4)
                emit("scvtf d0, w0");
            else if (val->ty->is_unsigned)
                emit("ucvtf d0, x0");
            else
                emit("scvtf d0, x0");
        }
        if (mty->kind == TY_FLOAT) {
            emit("fcvt s0, d0");
            emit("str s0, [x9, #%d]", off);
        } else {
            emit("str d0, [x9, #%d]", off);
        }
    } else if (mty && mty->size == 1) {
        emit("strb w0, [x9, #%d]", off);
    } else if (mty && mty->size == 2) {
        emit("strh w0, [x9, #%d]", off);
    } else if (mty && mty->size == 4) {
        emit("str w0, [x9, #%d]", off);
    } else {
        emit("str x0, [x9, #%d]", off);
    }
}

/*
 * emit_cl_init_struct_r - recursive init-list walker for compound
 * literals. Returns the next unconsumed element.
 */
static struct node *emit_cl_init_struct_r(struct node *elem,
                                          struct type *ty,
                                          int base_off, int cl_off)
{
    struct node *cur;
    struct node *val;

    if (elem == NULL || ty == NULL) {
        return elem;
    }

    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        struct member *mbr;

        mbr = ty->members;
        cur = elem;
        while (cur != NULL && mbr != NULL) {
            int off;
            struct type *mty;

            val = (cur->kind == ND_DESIG_INIT) ? cur->lhs : cur;
            if (cur->kind == ND_DESIG_INIT && cur->offset > 0) {
                off = base_off + cur->offset;
                mty = cur->ty ? cur->ty : mbr->ty;
            } else {
                off = base_off + mbr->offset;
                mty = mbr->ty;
            }
            if (val != NULL) {
                if (val->kind == ND_INIT_LIST &&
                    (mty == NULL || !type_is_aggregate(mty))) {
                    val = unwrap_single_init_value(val);
                }
                if (mbr->bit_width > 0 &&
                    !(cur->kind == ND_DESIG_INIT && cur->offset > 0)) {
                    int bw, bo;
                    unsigned long mask;

                    bw = mbr->bit_width;
                    bo = mbr->bit_offset;
                    mask = (bw >= 64) ? ~0UL : (1UL << bw) - 1;
                    gen_expr(val);
                    if (cl_off > 0) {
                        emit_sub_fp("x9", cl_off);
                    } else {
                        emit("mov x9, sp");
                    }
                    if (bw < 64) {
                        emit("and x2, x0, #0x%lx", mask);
                    } else {
                        emit("mov x2, x0");
                    }
                    if (bo + bw <= 32) {
                        emit("ldr w3, [x9, #%d]", off);
                        emit("bfi w3, w2, #%d, #%d", bo, bw);
                        emit("str w3, [x9, #%d]", off);
                    } else {
                        emit("ldr x3, [x9, #%d]", off);
                        emit("bfi x3, x2, #%d, #%d", bo, bw);
                        emit("str x3, [x9, #%d]", off);
                    }
                    cur = cur->next;
                    mbr = mbr->next;
                    if (ty->kind == TY_UNION) break;
                    continue;
                }

                if (mty != NULL && type_is_aggregate(mty)) {
                    if (val->kind == ND_INIT_LIST) {
                        emit_cl_init_struct_r(val->body, mty, off, cl_off);
                        cur = cur->next;
                    } else {
                        cur = emit_cl_init_struct_r(cur, mty, off, cl_off);
                    }
                    mbr = mbr->next;
                    if (ty->kind == TY_UNION) break;
                    continue;
                }

                gen_expr(val);
                if (cl_off > 0) {
                    emit_sub_fp("x9", cl_off);
                } else {
                    emit("mov x9, sp");
                }
                emit_cl_store(mty, off, val);
            }
            cur = cur->next;
            mbr = mbr->next;
            if (ty->kind == TY_UNION) break;
        }
        return cur;
    }

    if (ty->kind == TY_ARRAY && ty->base != NULL) {
        int idx;
        int esz;

        esz = ty->base->size;
        idx = 0;
        cur = elem;
        while (cur != NULL && idx < ty->array_len) {
            int off;

            off = base_off + idx * esz;
            val = (cur->kind == ND_DESIG_INIT) ? cur->lhs : cur;
            if (val != NULL) {
                if (val->kind == ND_INIT_LIST &&
                    (ty->base == NULL ||
                     !type_is_aggregate(ty->base))) {
                    val = unwrap_single_init_value(val);
                }
                if (string_literal_matches_type(ty, val)) {
                    gen_expr(val);
                    if (cl_off > 0) {
                        emit_sub_fp("x9", cl_off);
                    } else {
                        emit("mov x9, sp");
                    }
                    emit_cl_store(ty, off, val);
                    return cur->next;
                }

                if (type_is_aggregate(ty->base)) {
                    if (val->kind == ND_INIT_LIST) {
                        emit_cl_init_struct_r(val->body, ty->base,
                                              off, cl_off);
                        cur = cur->next;
                    } else {
                        cur = emit_cl_init_struct_r(cur, ty->base,
                                                    off, cl_off);
                    }
                    idx++;
                    continue;
                }

                gen_expr(val);
                if (cl_off > 0) {
                    emit_sub_fp("x9", cl_off);
                } else {
                    emit("mov x9, sp");
                }
                emit_cl_store(ty->base, off, val);
            }
            cur = cur->next;
            idx++;
        }
        return cur;
    }

    val = (elem->kind == ND_DESIG_INIT) ? elem->lhs : elem;
    if (val != NULL) {
        if (val->kind == ND_INIT_LIST &&
            (ty == NULL || !type_is_aggregate(ty))) {
            val = unwrap_single_init_value(val);
        }
        gen_expr(val);
        if (cl_off > 0) {
            emit_sub_fp("x9", cl_off);
        } else {
            emit("mov x9, sp");
        }
        emit_cl_store(ty, base_off, val);
    }
    return elem->next;
}

/*
 * emit_cl_init_struct - recursively initialize a struct/union/array
 * within a compound literal at base_off bytes from the compound
 * literal start.  cl_off is the frame offset used to reload x9.
 */
static void emit_cl_init_struct(struct node *init, struct type *ty,
                                int base_off, int cl_off)
{
    if (init == NULL || init->kind != ND_INIT_LIST || ty == NULL)
        return;
    (void)emit_cl_init_struct_r(init->body, ty, base_off, cl_off);
}

/*
 * gen_expr - generate code for an expression node.
 * Result is always left in x0.
 */
static void gen_expr(struct node *n)
{
    struct node *arg;
    int nargs;
    int lbl;
    int sz;
    int i;

    if (n == NULL) {
        return;
    }

    switch (n->kind) {
    case ND_NUM:
        emit_num(n->val);
        return;

    case ND_FNUM:
    {
        /* emit FP constant via literal pool in .rodata */
        int flit;
        union { double d; unsigned long u; } fpun;

        flit = fp_literal_count++;
        emit_comment("fp literal %d", flit);
        emit("adrp x0, .LF%d", flit);
        if (n->ty != NULL && type_is_fp128(n->ty)) {
            emit("ldr q0, [x0, :lo12:.LF%d]", flit);
        } else {
            emit("ldr d0, [x0, :lo12:.LF%d]", flit);
        }
        /* store the literal value for later emission */
        n->label_id = flit;
        fpun.d = n->fval;
        n->val = (long)fpun.u;
        return;
    }

    case ND_VAR:
        gen_addr(n);
        emit_load(n->ty);
        return;

    case ND_STR:
        /* string literal: reference the label */
        if (cc_pic_enabled) {
            pic_emit_string_addr(out, n->label_id);
        } else {
            emit("adrp x0, .LS%d", n->label_id);
            emit("add x0, x0, :lo12:.LS%d", n->label_id);
        }
        return;

    case ND_ASSIGN:
        if (n->lhs && n->lhs->kind == ND_MEMBER && n->lhs->bit_width > 0) {
            /* bitfield store: read-modify-write using bfi */
            int bw;
            int bo;
            unsigned long mask;

            bw = n->lhs->bit_width;
            bo = n->lhs->bit_offset;
            mask = (bw >= 64) ? ~0UL : (1UL << bw) - 1;

            emit_comment("bitfield store: width=%d offset=%d", bw, bo);
            /* compute address of storage unit -> push */
            gen_addr(n->lhs);
            push();
            /* evaluate rhs value -> x0 */
            gen_expr(n->rhs);
            /* mask the new value to bitfield width */
            if (bw < 64) {
                emit("and x2, x0, #0x%lx", mask);
            } else {
                emit("mov x2, x0");
            }
            /* pop address into x1 */
            pop("x1");
            /* read-modify-write: load, bfi, store */
            if (bo + bw <= 32) {
                emit("ldr w3, [x1]");
                emit("bfi w3, w2, #%d, #%d", bo, bw);
                emit("str w3, [x1]");
            } else {
                emit("ldr x3, [x1]");
                emit("bfi x3, x2, #%d, #%d", bo, bw);
                emit("str x3, [x1]");
            }
            /* return the stored value in x0, sign-extended for signed
             * bitfields (C requires assignment to evaluate to the
             * value of the lhs after conversion) */
            emit("mov x0, x2");
            if (n->lhs->ty && !n->lhs->ty->is_unsigned &&
                n->lhs->ty->kind != TY_ENUM && bw < 64) {
                int shift_amt;
                shift_amt = 64 - bw;
                emit("lsl x0, x0, #%d", shift_amt);
                emit("asr x0, x0, #%d", shift_amt);
            }
        } else if (n->lhs && n->lhs->ty &&
                   string_literal_matches_type(n->lhs->ty, n->rhs)) {
            /* array = string literal: copy string bytes,
             * zero-fill remaining array bytes */
            int arr_sz;
            int str_len;
            int elem_sz;
            int ci;

            arr_sz = n->lhs->ty->size;
            elem_sz = string_literal_elem_size(n->rhs);
            str_len = (int)n->rhs->val + elem_sz;
            if (str_len > arr_sz) str_len = arr_sz;

            gen_addr(n->lhs);
            push();
            gen_expr(n->rhs);
            pop("x1");
            /* copy string bytes from x0 to x1 */
            for (ci = 0; ci < str_len; ci++) {
                emit("ldrb w2, [x0, #%d]", ci);
                emit("strb w2, [x1, #%d]", ci);
            }
            /* zero remaining bytes */
            for (; ci < arr_sz; ci++) {
                emit("strb wzr, [x1, #%d]", ci);
            }
        } else if (n->rhs &&
                   (n->rhs->kind == ND_CALL ||
                    n->rhs->kind == ND_VA_ARG ||
                    n->rhs->kind == ND_STMT_EXPR) &&
                   n->lhs && n->lhs->ty &&
                   (n->lhs->ty->kind == TY_STRUCT ||
                    n->lhs->ty->kind == TY_UNION) &&
                   n->lhs->ty->size <= 16 &&
                   !(n->rhs->kind == ND_CALL &&
                     n->rhs->ty != NULL &&
                     is_hfa(n->rhs->ty))) {
            /* struct/union assignment from call, va_arg, or stmt_expr:
             * the value is in x0/x1, not as an address.
             * Store directly to the destination.
             * (HFA calls are handled above via the HFA path.) */
            int retsz;
            int ci;
            gen_addr(n->lhs);
            push();
            gen_expr(n->rhs);
            retsz = n->lhs->ty->size;
            if (retsz <= 8) {
                pop("x1");
                if (retsz == 1) {
                    emit("strb w0, [x1]");
                } else if (retsz == 2) {
                    emit("strh w0, [x1]");
                } else if (retsz == 4) {
                    emit("str w0, [x1]");
                } else if (retsz == 8) {
                    emit("str x0, [x1]");
                } else {
                    for (ci = 0; ci < retsz; ci++) {
                        if (ci == 0) {
                            emit("mov x9, x0");
                        } else {
                            emit("lsr x9, x0, #%d", ci * 8);
                        }
                        emit("strb w9, [x1, #%d]", ci);
                    }
                }
            } else {
                /* 9-16 bytes: x0 has low 8, x1 has high bytes.
                 * Save x1 before pop clobbers it.  Only write
                 * the actual remaining bytes to avoid overwriting
                 * memory past the struct (e.g. 12-byte struct). */
                int hi_sz;
                int bi;
                hi_sz = retsz - 8;
                emit("mov x9, x1");
                pop("x1");
                emit("str x0, [x1]");
                if (hi_sz == 8) {
                    emit("str x9, [x1, #8]");
                } else {
                    for (bi = 0; bi < hi_sz; bi++) {
                        if (bi == 0) {
                            emit("mov x10, x9");
                        } else {
                            emit("lsr x10, x9, #%d", bi * 8);
                        }
                        emit("strb w10, [x1, #%d]", 8 + bi);
                    }
                }
            }
        } else if (n->rhs &&
                   (n->rhs->kind == ND_CALL ||
                    n->rhs->kind == ND_STMT_EXPR) &&
                   n->lhs && n->lhs->ty &&
                   (n->lhs->ty->kind == TY_COMPLEX_DOUBLE ||
                    n->lhs->ty->kind == TY_COMPLEX_FLOAT)) {
            /* complex assignment from call/stmt_expr:
             * value is in d0/d1 (or s0/s1), not at an address.
             * Store directly to destination. */
            gen_addr(n->lhs);
            push();
            gen_expr(n->rhs);
            pop("x1");
            if (n->lhs->ty->kind == TY_COMPLEX_DOUBLE) {
                emit("str d0, [x1]");
                emit("str d1, [x1, #8]");
            } else {
                emit("str s0, [x1]");
                emit("str s1, [x1, #4]");
            }
            /* leave address in x0 for chained assignment */
            emit("mov x0, x1");
        } else if (n->lhs && n->lhs->ty &&
                   (n->lhs->ty->kind == TY_COMPLEX_DOUBLE ||
                    n->lhs->ty->kind == TY_COMPLEX_FLOAT) &&
                   n->rhs && n->rhs->ty &&
                   n->rhs->ty->kind != TY_COMPLEX_DOUBLE &&
                   n->rhs->ty->kind != TY_COMPLEX_FLOAT) {
            /* scalar-to-complex assignment: convert scalar to
             * real part, set imaginary part to 0. */
            int src_fp64;
            int src_fp128;
            gen_addr(n->lhs);
            push();
            gen_expr(n->rhs);
            src_fp128 = (n->rhs->ty != NULL && type_is_fp128(n->rhs->ty));
            src_fp64 = (n->rhs->ty != NULL && type_is_flonum(n->rhs->ty) &&
                        !src_fp128);
            if (src_fp128) {
                if (n->lhs->ty->kind == TY_COMPLEX_FLOAT) {
                    emit_fp128_to_float_in_d0();
                } else {
                    emit_fp128_to_double();
                }
            } else if (!src_fp64) {
                /* integer -> double for real part */
                if (n->rhs->ty && n->rhs->ty->is_unsigned)
                    emit("ucvtf d0, x0");
                else
                    emit("scvtf d0, x0");
            }
            /* d0 now has the real part value */
            pop("x1");
            if (n->lhs->ty->kind == TY_COMPLEX_DOUBLE) {
                emit("str d0, [x1]");
                emit("str xzr, [x1, #8]");
            } else {
                emit("fcvt s0, d0");
                emit("str s0, [x1]");
                emit("str wzr, [x1, #4]");
            }
            emit("mov x0, x1");
        } else if (n->lhs && n->lhs->ty &&
                   (n->lhs->ty->kind == TY_COMPLEX_DOUBLE ||
                    n->lhs->ty->kind == TY_COMPLEX_FLOAT) &&
                   n->rhs && n->rhs->ty &&
                   (n->rhs->ty->kind == TY_COMPLEX_DOUBLE ||
                    n->rhs->ty->kind == TY_COMPLEX_FLOAT) &&
                   n->rhs->kind != ND_VAR &&
                   n->rhs->kind != ND_MEMBER &&
                   n->rhs->kind != ND_DEREF) {
            /* complex-to-complex assignment from computed expression
             * (call, arithmetic, etc). Result is in d0/d1 (or s0/s1)
             * for calls, or address in x0 for arithmetic (with stack temp).
             * Generate dest addr first, then rhs, then store. */
            int rhs_in_regs;
            rhs_in_regs = (n->rhs->kind == ND_CALL ||
                           n->rhs->kind == ND_STMT_EXPR ||
                           n->rhs->kind == ND_ADD ||
                           n->rhs->kind == ND_SUB ||
                           n->rhs->kind == ND_MUL ||
                           n->rhs->kind == ND_DIV);
            gen_addr(n->lhs);
            push();
            gen_expr(n->rhs);
            if (n->rhs->kind == ND_CALL ||
                n->rhs->kind == ND_STMT_EXPR) {
                /* result in d0/d1 -- store directly */
                pop("x1");
                if (n->lhs->ty->kind == TY_COMPLEX_DOUBLE) {
                    emit("str d0, [x1]");
                    emit("str d1, [x1, #8]");
                } else {
                    emit("str s0, [x1]");
                    emit("str s1, [x1, #4]");
                }
            } else {
                /* result address in x0, possibly stack-allocated.
                 * Load components from [x0], free stack temp,
                 * then pop dest addr and store. */
                if (n->lhs->ty->kind == TY_COMPLEX_DOUBLE) {
                    emit("ldr d0, [x0]");
                    emit("ldr d1, [x0, #8]");
                } else {
                    emit("ldr s0, [x0]");
                    emit("ldr s1, [x0, #4]");
                }
                /* free the stack temp if arithmetic pushed it */
                if (rhs_in_regs) {
                    emit("add sp, sp, #16");
                    depth--;
                }
                pop("x1");
                if (n->lhs->ty->kind == TY_COMPLEX_DOUBLE) {
                    emit("str d0, [x1]");
                    emit("str d1, [x1, #8]");
                } else {
                    emit("str s0, [x1]");
                    emit("str s1, [x1, #4]");
                }
            }
            emit("mov x0, x1");
        } else if (n->rhs && n->rhs->kind == ND_INIT_LIST &&
                   n->lhs && n->lhs->ty) {
            /* init list assignment: zero the destination, then
             * store each element at the appropriate offset */
            int total_sz;
            int ci;

            total_sz = n->lhs->ty->size;
            gen_addr(n->lhs);
            push();
            /* zero-fill the entire destination */
            pop("x1");
            push(); /* keep address on stack */
            {
                int base;
                int step;
                emit("mov x9, x1");
                base = 0;
                for (ci = 0; ci + 8 <= total_sz; ci += 8) {
                    if (ci - base > 32752) {
                        step = ci - base;
                        if (step <= 4095) {
                            emit("add x9, x9, #%d", step);
                        } else {
                            emit_load_imm_reg("x10", step);
                            emit("add x9, x9, x10");
                        }
                        base = ci;
                    }
                    emit("str xzr, [x9, #%d]", ci - base);
                }
                for (; ci + 4 <= total_sz; ci += 4) {
                    if (ci - base > 16376) {
                        step = ci - base;
                        if (step <= 4095) {
                            emit("add x9, x9, #%d", step);
                        } else {
                            emit_load_imm_reg("x10", step);
                            emit("add x9, x9, x10");
                        }
                        base = ci;
                    }
                    emit("str wzr, [x9, #%d]", ci - base);
                }
                for (; ci < total_sz; ci++) {
                    if (ci - base > 4095) {
                        step = ci - base;
                        if (step <= 4095) {
                            emit("add x9, x9, #%d", step);
                        } else {
                            emit_load_imm_reg("x10", step);
                            emit("add x9, x9, x10");
                        }
                        base = ci;
                    }
                    emit("strb wzr, [x9, #%d]", ci - base);
                }
            }
            /* store each init list element using recursive helper */
            emit_il_init_struct(n->rhs, n->lhs->ty, 0);
            pop("x0"); /* pop destination address into x0 */
        } else if (n->rhs != NULL &&
                   n->rhs->lhs == n->lhs &&
                   (n->rhs->kind == ND_ADD || n->rhs->kind == ND_SUB ||
                    n->rhs->kind == ND_MUL || n->rhs->kind == ND_DIV ||
                    n->rhs->kind == ND_MOD || n->rhs->kind == ND_BITAND ||
                    n->rhs->kind == ND_BITOR || n->rhs->kind == ND_BITXOR ||
                    n->rhs->kind == ND_SHL || n->rhs->kind == ND_SHR)) {
            /* compound assignment: lhs op= rhs
             * evaluate lhs address once, then evaluate rhs,
             * then reload lhs value for correct sequencing with
             * side effects. */
            gen_addr(n->lhs);
            push(); /* push lhs addr */
            if (n->lhs->ty && type_is_fp128(n->lhs->ty)) {
                const char *fn;

                fn = NULL;
                if (n->rhs->kind == ND_ADD) fn = "__addtf3";
                else if (n->rhs->kind == ND_SUB) fn = "__subtf3";
                else if (n->rhs->kind == ND_MUL) fn = "__multf3";
                else if (n->rhs->kind == ND_DIV) fn = "__divtf3";

                /* fp128 compound assignment path */
                gen_expr(n->rhs->rhs);
                if (n->rhs->rhs->ty != NULL &&
                    !type_is_fp128(n->rhs->rhs->ty)) {
                    if (type_is_real_flonum(n->rhs->rhs->ty)) {
                        emit_double_to_fp128();
                    } else {
                        emit_int_to_fp128(n->rhs->rhs->ty);
                    }
                }
                push_q(); /* push rhs fp128 value */
                /* reload lhs addr and load lhs value
                 * stack: [sp]=rhs_q, [sp+16]=lhs_addr */
                emit("ldr x0, [sp, #16]");
                emit_load(n->lhs->ty); /* q0 = lhs */
                pop_q("q1");           /* q1 = rhs */
                if (fn != NULL) {
                    emit("bl %s", fn);
                }
            } else if (n->lhs->ty &&
                       type_is_real_flonum(n->lhs->ty)) {
                /* floating-point compound assignment path:
                 * evaluate rhs first, then load lhs value */
                gen_expr(n->rhs->rhs);
                /* if rhs is integer, convert to double */
                if (n->rhs->rhs->ty &&
                    !type_is_real_flonum(n->rhs->rhs->ty)) {
                    if (n->rhs->rhs->ty->is_unsigned &&
                        n->rhs->rhs->ty->size <= 4)
                        emit("ucvtf d0, w0");
                    else if (n->rhs->rhs->ty->size <= 4)
                        emit("scvtf d0, w0");
                    else if (n->rhs->rhs->ty->is_unsigned)
                        emit("ucvtf d0, x0");
                    else
                        emit("scvtf d0, x0");
                }
                push_fp(); /* push rhs fp value */
                /* reload lhs addr and load lhs value
                 * stack: [sp]=rhs_fp, [sp+16]=lhs_addr */
                emit("ldr x0, [sp, #16]"); /* peek lhs addr */
                if (n->lhs->kind == ND_MEMBER &&
                    n->lhs->bit_width > 0) {
                    emit_bitfield_load(n->lhs);
                } else {
                    emit_load(n->lhs->ty);
                }
                /* d0 = lhs, pop rhs into d1 */
                pop_fp("d1"); /* d1 = rhs value */
                switch (n->rhs->kind) {
                case ND_ADD:
                    emit("fadd d0, d0, d1"); break;
                case ND_SUB:
                    emit("fsub d0, d0, d1"); break;
                case ND_MUL:
                    emit("fmul d0, d0, d1"); break;
                case ND_DIV:
                    emit("fdiv d0, d0, d1"); break;
                default: break;
                }
            } else {
            /* evaluate rhs operand first (for correct side-effect
             * sequencing -- rhs may modify the lhs location) */
            gen_expr(n->rhs->rhs);
            /* if rhs is float/double but lhs is integer, convert */
            if (n->rhs->rhs->ty && type_is_fp128(n->rhs->rhs->ty)) {
                if (n->lhs->ty && n->lhs->ty->is_unsigned) {
                    emit("bl __fixunstfdi");
                } else {
                    emit("bl __fixtfdi");
                }
            } else if (n->rhs->rhs->ty &&
                       type_is_real_flonum(n->rhs->rhs->ty)) {
                if (n->lhs->ty && n->lhs->ty->is_unsigned) {
                    emit("fcvtzu x0, d0");
                } else {
                    emit("fcvtzs x0, d0");
                }
            }
            push(); /* push rhs value */
            /* reload lhs addr and load current lhs value */
            emit("ldr x0, [sp, #16]"); /* peek lhs addr from stack */
            if (n->lhs->kind == ND_MEMBER && n->lhs->bit_width > 0) {
                emit_bitfield_load(n->lhs);
            } else {
                emit_load(n->lhs->ty);
            }
            /* pop rhs value; x0 = lhs value after reload */
            pop("x1"); /* x1 = rhs value */
            /* swap: code below expects x0=rhs, x1=lhs */
            emit("mov x9, x0");
            emit("mov x0, x1");
            emit("mov x1, x9");
            {
            /* For compound assignment, use the promoted type for
             * the operation, not the storage type of LHS. Small
             * unsigned types (char, short) promote to signed int
             * per C integer promotion rules. */
            int op_unsigned;
            op_unsigned = (n->rhs->ty != NULL) ?
                n->rhs->ty->is_unsigned :
                (n->lhs->ty != NULL && n->lhs->ty->is_unsigned &&
                 n->lhs->ty->size >= 4);
            switch (n->rhs->kind) {
            case ND_ADD:
                /* pointer arithmetic: scale rhs by element size */
                if (n->lhs->ty != NULL &&
                    (n->lhs->ty->kind == TY_PTR ||
                     n->lhs->ty->kind == TY_ARRAY) &&
                    n->lhs->ty->base != NULL &&
                    n->lhs->ty->base->size > 1) {
                    emit("mov x2, #%d", n->lhs->ty->base->size);
                    emit("mul x0, x0, x2");
                }
                emit("add x0, x1, x0"); break;
            case ND_SUB:
                /* pointer arithmetic: scale rhs by element size */
                if (n->lhs->ty != NULL &&
                    (n->lhs->ty->kind == TY_PTR ||
                     n->lhs->ty->kind == TY_ARRAY) &&
                    n->lhs->ty->base != NULL &&
                    n->lhs->ty->base->size > 1) {
                    emit("mov x2, #%d", n->lhs->ty->base->size);
                    emit("mul x0, x0, x2");
                }
                emit("sub x0, x1, x0"); break;
            case ND_MUL:
                emit("mul x0, x1, x0"); break;
            case ND_DIV:
                if (op_unsigned)
                    emit("udiv x0, x1, x0");
                else
                    emit("sdiv x0, x1, x0");
                break;
            case ND_MOD:
                if (op_unsigned) {
                    emit("udiv x2, x1, x0");
                } else {
                    emit("sdiv x2, x1, x0");
                }
                emit("msub x0, x2, x0, x1");
                break;
            case ND_BITAND:
                emit("and x0, x1, x0"); break;
            case ND_BITOR:
                emit("orr x0, x1, x0"); break;
            case ND_BITXOR:
                emit("eor x0, x1, x0"); break;
            case ND_SHL:
                emit("lsl x0, x1, x0"); break;
            case ND_SHR:
                if (op_unsigned)
                    emit("lsr x0, x1, x0");
                else
                    emit("asr x0, x1, x0");
                break;
            default: break;
            }
            }
            }
            /* pop lhs address, store */
            pop("x1");
            if (n->lhs->kind == ND_MEMBER &&
                n->lhs->bit_width > 0) {
                emit_bitfield_store(n->lhs);
            } else {
                emit_store(n->lhs->ty);
            }
        } else if (n->rhs != NULL &&
                   (n->rhs->kind == ND_CALL ||
                    n->rhs->kind == ND_STMT_EXPR) &&
                   n->lhs && n->lhs->ty &&
                   (n->lhs->ty->kind == TY_STRUCT ||
                    n->lhs->ty->kind == TY_UNION) &&
                   n->lhs->ty->size > 16 &&
                   n->rhs->ty != NULL &&
                   !is_hfa(n->rhs->ty)) {
            /* large struct assignment from call (> 16 bytes):
             * Use a temp buffer to avoid aliasing issues.
             * The callee writes to x8 (temp), then we copy
             * from temp to the LHS destination. */
            {
                int rsz, ci2;
                rsz = align_to(n->lhs->ty->size, 16);
                /* allocate temp buffer FIRST */
                emit("sub sp, sp, #%d", rsz);
                depth += rsz / 16;
                emit("mov x8, sp");
                /* save LHS address above the buffer */
                gen_addr(n->lhs);
                push();
                x8_preset = 1;
                gen_expr(n->rhs);
                x8_preset = 0;
                /* x0 = temp buffer with result */
                emit("mov x2, x0");
                pop("x1"); /* LHS address */
                for (ci2 = 0; ci2 + 8 <= n->lhs->ty->size;
                     ci2 += 8) {
                    emit("ldr x3, [x2, #%d]", ci2);
                    emit("str x3, [x1, #%d]", ci2);
                }
                for (; ci2 + 4 <= n->lhs->ty->size; ci2 += 4) {
                    emit("ldr w3, [x2, #%d]", ci2);
                    emit("str w3, [x1, #%d]", ci2);
                }
                for (; ci2 < n->lhs->ty->size; ci2++) {
                    emit("ldrb w3, [x2, #%d]", ci2);
                    emit("strb w3, [x1, #%d]", ci2);
                }
                /* reclaim temp buffer */
                emit("add sp, sp, #%d", rsz);
                depth -= rsz / 16;
                emit("mov x0, x1"); /* result = LHS addr */
            }
        } else if (n->rhs != NULL && n->rhs->kind == ND_CALL &&
                   n->rhs->ty != NULL &&
                   (n->rhs->ty->kind == TY_STRUCT ||
                    n->rhs->ty->kind == TY_UNION) &&
                   is_hfa(n->rhs->ty)) {
            /* HFA struct assignment from function call:
             * result is in d0-d3 (or s0-s3), store directly */
            int hfa_k3, hcnt3, mi3, msz3;
            hfa_k3 = is_hfa(n->rhs->ty);
            hcnt3 = hfa_member_count(n->rhs->ty);
            msz3 = hfa_k3;
            gen_addr(n->lhs);
            push();
            gen_expr(n->rhs);
            pop("x1");
            for (mi3 = 0; mi3 < hcnt3; mi3++) {
                if (hfa_k3 == 4) {
                    emit("str s%d, [x1, #%d]", mi3, mi3 * msz3);
                } else if (hfa_k3 == 16) {
                    emit("str q%d, [x1, #%d]", mi3, mi3 * msz3);
                } else {
                    emit("str d%d, [x1, #%d]", mi3, mi3 * msz3);
                }
            }
        } else {
            /* evaluate address of lhs, push; evaluate rhs; pop addr */
            int rhs_is_agg;
            int lhs_is_agg;
            struct type *store_ty;
            struct type *first_ty;
            int rhs_fp64, rhs_fp128;
            int store_fp64, store_fp128;

            gen_addr(n->lhs);
            push();
            gen_expr(n->rhs);

            /* Determine store type: normally lhs type, but allow
             * scalar -> struct/union to store into the first member. */
            lhs_is_agg = (n->lhs->ty &&
                          (n->lhs->ty->kind == TY_STRUCT ||
                           n->lhs->ty->kind == TY_UNION));
            rhs_is_agg = (n->rhs && n->rhs->ty &&
                          (n->rhs->ty->kind == TY_STRUCT ||
                           n->rhs->ty->kind == TY_UNION ||
                           n->rhs->ty->kind == TY_ARRAY));
            store_ty = n->lhs->ty;
            first_ty = NULL;
            if (lhs_is_agg && !rhs_is_agg &&
                n->lhs->ty && n->lhs->ty->members) {
                first_ty = n->lhs->ty->members->ty;
                if (first_ty != NULL) {
                    store_ty = first_ty;
                }
            }

            rhs_fp128 = (n->rhs && n->rhs->ty &&
                         type_is_fp128(n->rhs->ty));
            rhs_fp64 = (n->rhs && n->rhs->ty &&
                        type_is_real_flonum(n->rhs->ty) &&
                        !rhs_fp128);
            store_fp128 = (store_ty != NULL && type_is_fp128(store_ty));
            store_fp64 = (store_ty != NULL &&
                          type_is_real_flonum(store_ty) &&
                          !store_fp128);

            /* Convert rhs value to the store type (if scalar). */
            if (store_fp128) {
                if (!rhs_fp128) {
                    if (rhs_fp64) {
                        emit_double_to_fp128();
                    } else {
                        emit_int_to_fp128(n->rhs ? n->rhs->ty : NULL);
                    }
                }
            } else if (store_fp64) {
                if (rhs_fp128) {
                    if (store_ty->kind == TY_FLOAT) {
                        emit_fp128_to_float_in_d0();
                    } else {
                        emit_fp128_to_double();
                    }
                } else if (!rhs_fp64) {
                    if (n->rhs && n->rhs->ty &&
                        n->rhs->ty->is_unsigned &&
                        n->rhs->ty->size <= 4)
                        emit("ucvtf d0, w0");
                    else if (n->rhs && n->rhs->ty &&
                             n->rhs->ty->size <= 4)
                        emit("scvtf d0, w0");
                    else if (n->rhs && n->rhs->ty &&
                             n->rhs->ty->is_unsigned)
                        emit("ucvtf d0, x0");
                    else
                        emit("scvtf d0, x0");
                }
            } else {
                /* integer/pointer store */
                if (rhs_fp128) {
                    if (store_ty != NULL && store_ty->is_unsigned) {
                        emit("bl __fixunstfdi");
                    } else {
                        emit("bl __fixtfdi");
                    }
                } else if (rhs_fp64) {
                    int dst32;
                    dst32 = (store_ty != NULL && store_ty->size <= 4);
                    if (store_ty != NULL && store_ty->is_unsigned) {
                        emit(dst32 ? "fcvtzu w0, d0" : "fcvtzu x0, d0");
                    } else {
                        emit(dst32 ? "fcvtzs w0, d0" : "fcvtzs x0, d0");
                    }
                }

                /* Widen rhs to match 64-bit destination when rhs is
                 * a narrow integer (e.g. int -> unsigned long). */
                if (!rhs_fp64 && !rhs_fp128 &&
                    n->rhs && n->rhs->ty &&
                    store_ty != NULL &&
                    store_ty->size == 8 &&
                    n->rhs->ty->size <= 4 &&
                    n->rhs->ty->kind != TY_PTR &&
                    store_ty->kind != TY_PTR &&
                    store_ty->kind != TY_STRUCT &&
                    store_ty->kind != TY_UNION) {
                    if (n->rhs->ty->is_unsigned) {
                        emit("mov w0, w0");
                    } else {
                        emit("sxtw x0, w0");
                    }
                }
            }

            pop("x1");
            emit_store(store_ty);
        }
        /* truncate/extend x0 to match lhs type so that
         * chained assignments (a = b = val) see the correct
         * value after implicit conversion to the assigned type */
        if (n->lhs && n->lhs->ty &&
            n->lhs->ty->kind != TY_STRUCT &&
            n->lhs->ty->kind != TY_UNION &&
            n->lhs->ty->kind != TY_ARRAY &&
            !type_is_fp_scalar(n->lhs->ty)) {
            int lsz;
            lsz = n->lhs->ty->size;
            if (lsz == 1) {
                if (n->lhs->ty->is_unsigned)
                    emit("and x0, x0, #0xff");
                else
                    emit("sxtb x0, w0");
            } else if (lsz == 2) {
                if (n->lhs->ty->is_unsigned)
                    emit("and x0, x0, #0xffff");
                else
                    emit("sxth x0, w0");
            } else if (lsz == 4) {
                if (n->lhs->ty->is_unsigned)
                    emit("mov w0, w0");
                else
                    emit("sxtw x0, w0");
            }
        }
        return;

    case ND_ADDR:
        gen_addr(n->lhs);
        return;

    case ND_DEREF:
        gen_expr(n->lhs);
        emit_load(n->ty);
        return;

    case ND_MEMBER:
        /* Complex member access on function call result:
         * __real foo() or __imag foo() where foo returns complex.
         * Result is in d0/d1 (or s0/s1). */
        if (n->lhs && n->lhs->kind == ND_CALL && n->bit_width == 0
            && n->lhs->ty
            && n->lhs->ty->kind == TY_COMPLEX_DOUBLE) {
            gen_expr(n->lhs);
            if (n->offset == 8) {
                emit("fmov d0, d1");
            }
            return;
        }
        if (n->lhs && n->lhs->kind == ND_CALL && n->bit_width == 0
            && n->lhs->ty
            && n->lhs->ty->kind == TY_COMPLEX_FLOAT) {
            gen_expr(n->lhs);
            if (n->offset == 4) {
                emit("fmov s0, s1");
            }
            emit("fcvt d0, s0");
            return;
        }
        /* HFA member access on function call result */
        if (n->lhs && n->lhs->kind == ND_CALL && n->bit_width == 0
            && n->lhs->ty
            && (n->lhs->ty->kind == TY_STRUCT
                || n->lhs->ty->kind == TY_UNION)
            && is_hfa(n->lhs->ty)) {
            int hfa_k2, msz2, mi2;
            hfa_k2 = is_hfa(n->lhs->ty);
            msz2 = hfa_k2;
            mi2 = n->offset / msz2;
            gen_expr(n->lhs);
            if (hfa_k2 == 4) {
                if (mi2 != 0) {
                    emit("fmov s0, s%d", mi2);
                }
                emit("fcvt d0, s0");
            } else if (hfa_k2 == 16) {
                if (mi2 != 0) {
                    emit("mov v0.16b, v%d.16b", mi2);
                }
            } else {
                if (mi2 != 0) {
                    emit("fmov d0, d%d", mi2);
                }
            }
            return;
        }
        /* Special case: member access on function call result.
         * The struct is returned in registers (x0/x1), so extract
         * the member directly without going through gen_addr. */
        if (n->lhs && n->lhs->kind == ND_CALL && n->bit_width == 0
            && n->lhs->ty
            && (n->lhs->ty->kind == TY_STRUCT
                || n->lhs->ty->kind == TY_UNION)
            && n->lhs->ty->size <= 16) {
            int mbr_off;
            int mbr_sz;
            gen_expr(n->lhs);
            mbr_off = n->offset;
            mbr_sz = n->ty ? n->ty->size : 4;
            if (n->lhs->ty->size <= 8) {
                /* struct in x0: extract member by shift+mask */
                if (mbr_off > 0) {
                    emit("lsr x0, x0, #%d", mbr_off * 8);
                }
                if (mbr_sz == 1) {
                    emit("and x0, x0, #0xff");
                    if (n->ty && !n->ty->is_unsigned) {
                        emit("sxtb x0, w0");
                    }
                } else if (mbr_sz == 2) {
                    emit("and x0, x0, #0xffff");
                    if (n->ty && !n->ty->is_unsigned) {
                        emit("sxth x0, w0");
                    }
                } else if (mbr_sz == 4) {
                    if (mbr_off > 0) {
                        emit("and x0, x0, #0xffffffff");
                    }
                    if (n->ty && !n->ty->is_unsigned) {
                        emit("sxtw x0, w0");
                    }
                }
                /* 8-byte member: no masking needed */
            } else {
                /* struct in x0/x1: pick register based on offset */
                if (mbr_off >= 8) {
                    emit("mov x0, x1");
                    mbr_off -= 8;
                }
                if (mbr_off > 0) {
                    emit("lsr x0, x0, #%d", mbr_off * 8);
                }
                if (mbr_sz == 1) {
                    emit("and x0, x0, #0xff");
                    if (n->ty && !n->ty->is_unsigned) {
                        emit("sxtb x0, w0");
                    }
                } else if (mbr_sz == 2) {
                    emit("and x0, x0, #0xffff");
                    if (n->ty && !n->ty->is_unsigned) {
                        emit("sxth x0, w0");
                    }
                } else if (mbr_sz == 4) {
                    if (mbr_off > 0) {
                        emit("and x0, x0, #0xffffffff");
                    }
                    if (n->ty && !n->ty->is_unsigned) {
                        emit("sxtw x0, w0");
                    }
                }
            }
            return;
        }
        gen_addr(n);
        if (n->bit_width > 0) {
            /* bitfield load: load storage unit, shift right, mask */
            emit_comment("bitfield load: width=%d offset=%d",
                         n->bit_width, n->bit_offset);
            if (n->bit_offset + n->bit_width <= 32) {
                emit("ldr w0, [x0]");
            } else {
                emit("ldr x0, [x0]");
            }
            if (n->bit_offset > 0) {
                emit("lsr x0, x0, #%d", n->bit_offset);
            }
            if (n->bit_width < 64) {
                unsigned long mask;
                mask = (1UL << n->bit_width) - 1;
                emit("and x0, x0, #0x%lx", mask);
            }
            /* sign extension for signed bitfields
             * (enum bitfields are treated as unsigned) */
            if (n->ty && !n->ty->is_unsigned &&
                n->ty->kind != TY_ENUM) {
                int shift_amt;
                shift_amt = 64 - n->bit_width;
                emit("lsl x0, x0, #%d", shift_amt);
                emit("asr x0, x0, #%d", shift_amt);
            }
        } else {
            emit_load(n->ty);
        }
        return;

    case ND_LOGNOT:
        gen_expr(n->lhs);
        emit_truth_test(n->lhs);
        emit("eor x0, x0, #1");
        return;

    case ND_BITNOT:
        gen_expr(n->lhs);
        if (is_32bit(n->ty)) {
            emit("mvn w0, w0");
        } else {
            emit("mvn x0, x0");
        }
        if (n->lhs != NULL && n->lhs->kind == ND_MEMBER &&
            n->lhs->bit_width > 0) {
            emit_bitfield_truncate_width(n->lhs->bit_width,
                                         n->lhs->ty);
        }
        return;

    case ND_LOGAND:
        lbl = new_label();
        gen_expr(n->lhs);
        emit_truth_test(n->lhs);
        emit("cmp x0, #0");
        emit("b.eq .L.false.%d", lbl);
        gen_expr(n->rhs);
        emit_truth_test(n->rhs);
        emit("cmp x0, #0");
        emit("b.eq .L.false.%d", lbl);
        emit("mov x0, #1");
        emit("b .L.end.%d", lbl);
        emit_label(".L.false.%d", lbl);
        emit("mov x0, #0");
        emit_label(".L.end.%d", lbl);
        return;

    case ND_LOGOR:
        lbl = new_label();
        gen_expr(n->lhs);
        emit_truth_test(n->lhs);
        emit("cmp x0, #0");
        emit("b.ne .L.true.%d", lbl);
        gen_expr(n->rhs);
        emit_truth_test(n->rhs);
        emit("cmp x0, #0");
        emit("b.ne .L.true.%d", lbl);
        emit("mov x0, #0");
        emit("b .L.end.%d", lbl);
        emit_label(".L.true.%d", lbl);
        emit("mov x0, #1");
        emit_label(".L.end.%d", lbl);
        return;

    case ND_CALL:
        /* alloca(size) / __builtin_alloca(size) -> sub sp */
        if (n->name != NULL &&
            (strcmp(n->name, "alloca") == 0 ||
             strcmp(n->name, "__builtin_alloca") == 0)) {
            if (n->args) {
                gen_expr(n->args);
            }
            /* align size to 16 bytes */
            emit("add x0, x0, #15");
            emit("and x0, x0, #-16");
            if (depth > 0) {
                /* There are push-stack temporaries on the stack.
                 * We must relocate them below the alloca region.
                 * x0 = aligned alloca size. Copy each pushed
                 * slot from [sp + di*16] to [sp - x0 + di*16],
                 * then adjust sp. */
                int di;
                emit("mov x9, x0");
                for (di = 0; di < depth; di++) {
                    emit("ldr x10, [sp, #%d]", di * 16);
                    emit("sub x11, sp, x9");
                    emit("str x10, [x11, #%d]", di * 16);
                }
                emit("sub sp, sp, x9");
                /* alloca result is above the relocated push stack */
                emit("add x0, sp, #%d", depth * 16);
            } else {
                emit("sub sp, sp, x0");
                emit("mov x0, sp");
            }
            return;
        }
        /* __builtin_prefetch(addr, ...) -> prfm pldl1keep, [addr] */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_prefetch") == 0) {
            if (n->args) {
                gen_expr(n->args);
            }
            emit("prfm pldl1keep, [x0]");
            return;
        }
        /* __builtin_frame_address(0) -> mov x0, x29 */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_frame_address") == 0) {
            emit("mov x0, x29");
            return;
        }
        /* __builtin_return_address(N) -> load saved LR from Nth frame */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_return_address") == 0) {
            int ra_depth;
            ra_depth = (int)n->val;
            if (ra_depth == 0) {
                if (current_is_leaf) {
                    /* leaf function: LR is still in x30 */
                    emit("mov x0, x30");
                } else {
                    /* LR saved at [fp, #8] by stp x29,x30 prologue */
                    emit("ldr x0, [x29, #8]");
                }
            } else {
                /* walk up N frames via saved frame pointers */
                int ri;
                if (current_is_leaf) {
                    /* leaf: x29 is caller's fp, not ours */
                    emit("mov x0, x29");
                    for (ri = 1; ri < ra_depth; ri++) {
                        emit("ldr x0, [x0]");
                    }
                } else {
                    emit("mov x0, x29");
                    for (ri = 0; ri < ra_depth; ri++) {
                        emit("ldr x0, [x0]");
                    }
                }
                emit("ldr x0, [x0, #8]"); /* load saved LR */
            }
            return;
        }
        /* __builtin_trap() -> brk (software breakpoint) */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_trap") == 0) {
            emit("brk #0xf000");
            return;
        }
        /* __builtin_unreachable() -> brk (trap on unreachable) */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_unreachable") == 0) {
            emit("brk #0x1");
            return;
        }

        /* __sync_synchronize() -> dmb ish */
        if (n->name != NULL &&
            strcmp(n->name, "__sync_synchronize") == 0) {
            emit("dmb ish");
            return;
        }

        /*
         * __atomic_* builtins: memory-order-aware codegen.
         * The last (or second-to-last for CAS) argument is
         * the compile-time memorder constant.
         */
        if (n->name != NULL &&
            strncmp(n->name, "__atomic_", 9) == 0) {
            int bi_id;
            int memorder;
            struct node *a;
            struct node *mo_arg;
            int ac;

            bi_id = builtin_lookup(n->name);
            if (bi_id != 0 && builtin_is_atomic(bi_id)) {
                /* find the memorder argument */
                memorder = 5; /* default SEQ_CST */
                mo_arg = NULL;
                ac = 0;

                if (strcmp(n->name,
                    "__atomic_compare_exchange_n") == 0) {
                    /* succ memorder = 5th arg (index 4) */
                    ac = 0;
                    for (a = n->args; a; a = a->next) {
                        ac++;
                        if (ac == 5) mo_arg = a;
                    }
                } else {
                    /* last arg is memorder */
                    for (a = n->args; a; a = a->next)
                        mo_arg = a;
                }
                if (mo_arg && mo_arg->kind == ND_NUM)
                    memorder = (int)mo_arg->val;

                /* evaluate data args into registers */
                if (strcmp(n->name,
                        "__atomic_load_n") == 0) {
                    /* ptr -> x0 */
                    gen_expr(n->args);
                } else if (strcmp(n->name,
                        "__atomic_store_n") == 0) {
                    /* ptr -> x0, val -> x1 */
                    gen_expr(n->args);
                    push();
                    gen_expr(n->args->next);
                    emit("mov x1, x0");
                    pop("x0");
                } else if (strcmp(n->name,
                    "__atomic_compare_exchange_n") == 0) {
                    /* ptr -> x0, expected_ptr -> x1,
                     * desired -> x2 */
                    gen_expr(n->args);
                    push();
                    gen_expr(n->args->next);
                    push();
                    gen_expr(n->args->next->next);
                    emit("mov x2, x0");
                    pop("x1");
                    pop("x0");
                } else {
                    /* xchg, RMW: ptr -> x0, val -> x1 */
                    gen_expr(n->args);
                    push();
                    gen_expr(n->args->next);
                    emit("mov x1, x0");
                    pop("x0");
                }

                builtin_emit_atomic(out, bi_id, memorder);
                return;
            }
        }

        /* __builtin_clz(x) -> clz w0, w0 (32-bit) */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_clz") == 0) {
            gen_expr(n->args);
            emit("clz w0, w0");
            return;
        }
        /* __builtin_clzl(x) / __builtin_clzll(x) -> clz x0, x0 */
        if (n->name != NULL &&
            (strcmp(n->name, "__builtin_clzl") == 0 ||
             strcmp(n->name, "__builtin_clzll") == 0)) {
            gen_expr(n->args);
            emit("clz x0, x0");
            return;
        }

        /* __builtin_clrsb(x) -> cls w0, w0 (32-bit) */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_clrsb") == 0) {
            gen_expr(n->args);
            emit("cls w0, w0");
            return;
        }
        /* __builtin_clrsbl(x) / __builtin_clrsbll(x) -> cls x0, x0 */
        if (n->name != NULL &&
            (strcmp(n->name, "__builtin_clrsbl") == 0 ||
             strcmp(n->name, "__builtin_clrsbll") == 0)) {
            gen_expr(n->args);
            emit("cls x0, x0");
            return;
        }

        /* __builtin_ctz(x) -> rbit + clz (32-bit) */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_ctz") == 0) {
            gen_expr(n->args);
            emit("rbit w0, w0");
            emit("clz w0, w0");
            return;
        }
        /* __builtin_ctzl(x) / __builtin_ctzll(x) -> rbit + clz */
        if (n->name != NULL &&
            (strcmp(n->name, "__builtin_ctzl") == 0 ||
             strcmp(n->name, "__builtin_ctzll") == 0)) {
            gen_expr(n->args);
            emit("rbit x0, x0");
            emit("clz x0, x0");
            return;
        }

        /* __builtin_ffs(x) -> ctz+1 with zero check (32-bit) */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_ffs") == 0) {
            gen_expr(n->args);
            emit("and w0, w0, w0"); /* ensure 32-bit */
            emit("cmp w0, #0");
            emit("rbit w1, w0");
            emit("clz w1, w1");
            emit("add w1, w1, #1");
            emit("csel w0, w1, wzr, ne");
            return;
        }
        /* __builtin_ffsl(x) / __builtin_ffsll(x) -> ctz+1 (64-bit) */
        if (n->name != NULL &&
            (strcmp(n->name, "__builtin_ffsl") == 0 ||
             strcmp(n->name, "__builtin_ffsll") == 0)) {
            gen_expr(n->args);
            emit("cmp x0, #0");
            emit("rbit x1, x0");
            emit("clz x1, x1");
            emit("add x1, x1, #1");
            emit("csel x0, x1, xzr, ne");
            return;
        }

        /* __builtin_popcount(x) -> NEON cnt */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_popcount") == 0) {
            gen_expr(n->args);
            emit("fmov d0, x0");
            emit("cnt v0.8b, v0.8b");
            emit("addv b0, v0.8b");
            emit("fmov w0, s0");
            return;
        }
        /* __builtin_popcountl(x) / __builtin_popcountll(x) -> NEON cnt */
        if (n->name != NULL &&
            (strcmp(n->name, "__builtin_popcountl") == 0 ||
             strcmp(n->name, "__builtin_popcountll") == 0)) {
            gen_expr(n->args);
            emit("fmov d0, x0");
            emit("cnt v0.8b, v0.8b");
            emit("addv b0, v0.8b");
            emit("fmov w0, s0");
            return;
        }

        /* __builtin_bswap16(x) -> rev16 w0, w0; mask to 16 bits */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_bswap16") == 0) {
            gen_expr(n->args);
            emit("rev16 w0, w0");
            emit("and w0, w0, #0xffff");
            return;
        }
        /* __builtin_bswap32(x) -> rev w0, w0 */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_bswap32") == 0) {
            gen_expr(n->args);
            emit("rev w0, w0");
            return;
        }
        /* __builtin_bswap64(x) -> rev x0, x0 */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_bswap64") == 0) {
            gen_expr(n->args);
            emit("rev x0, x0");
            return;
        }

        /* __builtin_constant_p(x) -> always 0 at runtime */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_constant_p") == 0) {
            emit("mov x0, #0");
            return;
        }

        /* __builtin_expect(x, y) -> evaluate x, ignore y */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_expect") == 0) {
            gen_expr(n->args);
            return;
        }

        /* __builtin_expect_with_probability(x, v, p) -> just x */
        if (n->name != NULL &&
            strcmp(n->name,
                   "__builtin_expect_with_probability") == 0) {
            gen_expr(n->args);
            return;
        }

        /* __builtin_abs(x) / abs(x) -> cmp + cneg (32-bit) */
        if (n->name != NULL &&
            (strcmp(n->name, "__builtin_abs") == 0 ||
             strcmp(n->name, "abs") == 0)) {
            gen_expr(n->args);
            emit("cmp w0, #0");
            emit("cneg w0, w0, lt");
            /* sign-extend to 64-bit for return in x0 */
            emit("sxtw x0, w0");
            return;
        }
        /* __builtin_labs(x) / __builtin_llabs(x) / llabs(x)
         * -> cmp + cneg (64-bit) */
        if (n->name != NULL &&
            (strcmp(n->name, "__builtin_labs") == 0 ||
             strcmp(n->name, "__builtin_llabs") == 0 ||
             strcmp(n->name, "llabs") == 0)) {
            gen_expr(n->args);
            emit("cmp x0, #0");
            emit("cneg x0, x0, lt");
            return;
        }

        /* __builtin_fabs(x) -> fabs d0, d0 */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_fabs") == 0) {
            gen_expr(n->args);
            emit("fabs d0, d0");
            return;
        }
        /* __builtin_fabsf(x) -> fabs s0, s0 */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_fabsf") == 0) {
            gen_expr(n->args);
            emit("fabs s0, s0");
            return;
        }

        /* __builtin_sqrt(x) -> fsqrt d0, d0 */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_sqrt") == 0) {
            gen_expr(n->args);
            emit("fsqrt d0, d0");
            return;
        }
        /* __builtin_sqrtf(x) -> fsqrt s0, s0 */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_sqrtf") == 0) {
            gen_expr(n->args);
            emit("fsqrt s0, s0");
            return;
        }

        /* __builtin_signbit(x) -> check sign bit of double */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_signbit") == 0) {
            gen_expr(n->args);
            emit("fmov x0, d0");
            emit("lsr x0, x0, #63");
            return;
        }
        /* __builtin_signbitf(x) -> check sign bit of float */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_signbitf") == 0) {
            gen_expr(n->args);
            emit("fmov w0, s0");
            emit("lsr w0, w0, #31");
            return;
        }

        /* __builtin_copysign(x, y) -> copy sign of y to x (double) */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_copysign") == 0) {
            gen_expr(n->args);       /* x -> d0 */
            push_fp();               /* save x */
            gen_expr(n->args->next); /* y -> d0 */
            emit("fmov d1, d0");     /* d1 = y */
            pop_fp("d0");            /* d0 = x */
            /* clear sign of x, extract sign of y, OR them */
            emit("fmov x1, d1");
            emit("fmov x0, d0");
            emit("and x0, x0, #0x7fffffffffffffff"); /* |x| */
            emit("and x1, x1, #0x8000000000000000"); /* sign(y) */
            emit("orr x0, x0, x1");
            emit("fmov d0, x0");
            return;
        }
        /* __builtin_copysignf(x, y) -> copy sign (float) */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_copysignf") == 0) {
            gen_expr(n->args);       /* x -> s0 */
            push_fp();               /* save x */
            gen_expr(n->args->next); /* y -> s0 */
            emit("fmov s1, s0");     /* s1 = y */
            pop_fp("d0");            /* d0/s0 = x */
            emit("fmov w1, s1");
            emit("fmov w0, s0");
            emit("and w0, w0, #0x7fffffff"); /* |x| */
            emit("and w1, w1, #0x80000000"); /* sign(y) */
            emit("orr w0, w0, w1");
            emit("fmov s0, w0");
            return;
        }

        /* __builtin_conj* (complex conjugate) */
        if (n->name != NULL &&
            (strcmp(n->name, "__builtin_conj") == 0 ||
             strcmp(n->name, "__builtin_conjf") == 0 ||
             strcmp(n->name, "__builtin_conjl") == 0)) {
            gen_expr(n->args);
            if (n->ty != NULL && n->ty->kind == TY_COMPLEX_FLOAT) {
                emit("ldr s0, [x0]");
                emit("ldr s1, [x0, #4]");
                emit("fneg s1, s1");
                emit("sub sp, sp, #16");
                depth++;
                emit("str s0, [sp]");
                emit("str s1, [sp, #4]");
                emit("mov x0, sp");
            } else {
                emit("ldr d0, [x0]");
                emit("ldr d1, [x0, #8]");
                emit("fneg d1, d1");
                emit("sub sp, sp, #16");
                depth++;
                emit("str d0, [sp]");
                emit("str d1, [sp, #8]");
                emit("mov x0, sp");
            }
            return;
        }

        /* __builtin_isnan(x) -> fcmp d0, d0; unordered if NaN */
        if (n->name != NULL &&
            (strcmp(n->name, "__builtin_isnan") == 0 ||
             strcmp(n->name, "__builtin_isnanf") == 0)) {
            gen_expr(n->args);
            /* if arg is float, widen to double first */
            if (n->args->ty &&
                n->args->ty->kind == TY_FLOAT) {
                emit("fcvt d0, s0");
            }
            emit("fcmp d0, d0");
            emit("cset w0, vs");
            emit("mov x0, x0"); /* zero-extend */
            return;
        }

        /* __builtin_isinf(x) -> check for +/-Inf */
        if (n->name != NULL &&
            (strcmp(n->name, "__builtin_isinf") == 0 ||
             strcmp(n->name, "__builtin_isinff") == 0)) {
            gen_expr(n->args);
            if (n->args->ty &&
                n->args->ty->kind == TY_FLOAT) {
                emit("fcvt d0, s0");
            }
            emit("fmov x0, d0");
            /* mask off sign bit, check if == 0x7FF0000000000000 */
            emit("and x0, x0, #0x7fffffffffffffff");
            emit("mov x1, #0");
            emit("movk x1, #0x7FF0, lsl #48");
            emit("cmp x0, x1");
            emit("cset w0, eq");
            emit("mov x0, x0");
            return;
        }
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_isinfl") == 0) {
            gen_expr(n->args);
            emit_fp128_isinf();
            return;
        }

        /* __builtin_isfinite(x) -> not NaN and not Inf */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_isfinite") == 0) {
            gen_expr(n->args);
            if (n->args->ty &&
                n->args->ty->kind == TY_FLOAT) {
                emit("fcvt d0, s0");
            }
            emit("fmov x0, d0");
            /* extract exponent: if all 1s (0x7FF), not finite */
            emit("and x0, x0, #0x7fffffffffffffff");
            emit("mov x1, #0");
            emit("movk x1, #0x7FF0, lsl #48");
            emit("cmp x0, x1");
            emit("cset w0, lt");
            emit("mov x0, x0");
            return;
        }

        /* __builtin_isinf_sign(x) -> +1/-1 for inf, 0 otherwise */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_isinf_sign") == 0) {
            int lbl_notinf;
            int lbl_done;
            lbl_notinf = new_label();
            lbl_done = new_label();
            gen_expr(n->args);
            if (n->args->ty &&
                n->args->ty->kind == TY_FLOAT) {
                emit("fcvt d0, s0");
            }
            emit("fmov x0, d0");
            emit("mov x2, x0");
            emit("and x0, x0, #0x7fffffffffffffff");
            emit("mov x1, #0");
            emit("movk x1, #0x7FF0, lsl #48");
            emit("cmp x0, x1");
            emit("b.ne .L.%d", lbl_notinf);
            emit("asr x0, x2, #63");
            emit("orr x0, x0, #1");
            emit("b .L.%d", lbl_done);
            emit_label(".L.%d", lbl_notinf);
            emit("mov x0, #0");
            emit_label(".L.%d", lbl_done);
            return;
        }

        /* __builtin_signbitl(x) -> same as signbit */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_signbitl") == 0) {
            gen_expr(n->args);
            emit("fmov x0, d0");
            emit("lsr x0, x0, #63");
            return;
        }

        /* __builtin_copysignl(x, y) -> same as copysign */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_copysignl") == 0) {
            gen_expr(n->args);
            push_fp();
            gen_expr(n->args->next);
            emit("fmov d1, d0");
            pop_fp("d0");
            emit("fmov x1, d1");
            emit("fmov x0, d0");
            emit("and x0, x0, #0x7fffffffffffffff");
            emit("and x1, x1, #0x8000000000000000");
            emit("orr x0, x0, x1");
            emit("fmov d0, x0");
            return;
        }

        /* __builtin_fabsl(x) -> fabs d0, d0 */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_fabsl") == 0) {
            gen_expr(n->args);
            emit("fabs d0, d0");
            return;
        }

        /* __builtin_parityl(x), __builtin_parityll(x) */
        if (n->name != NULL &&
            (strcmp(n->name, "__builtin_parityl") == 0 ||
             strcmp(n->name, "__builtin_parityll") == 0)) {
            gen_expr(n->args);
            emit("fmov d0, x0");
            emit("cnt v0.8b, v0.8b");
            emit("addv b0, v0.8b");
            emit("fmov w0, s0");
            emit("and w0, w0, #1");
            return;
        }

        /* __builtin_parity(x) -- 32-bit popcount & 1 */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_parity") == 0) {
            gen_expr(n->args);
            emit("fmov s0, w0");
            emit("cnt v0.8b, v0.8b");
            emit("addv b0, v0.8b");
            emit("fmov w0, s0");
            emit("and w0, w0, #1");
            return;
        }

        /* __builtin_longjmp(buf, val) */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_longjmp") == 0) {
            gen_expr(n->args);
            push();
            gen_expr(n->args->next);
            emit("mov x1, x0");
            pop("x0");
            /* GCC __builtin_longjmp: value is always 1 */
            emit("mov x1, #1");
            emit("ldp x29, x30, [x0]");
            emit("ldr x2, [x0, #16]");
            emit("mov sp, x2");
            emit("mov x0, x1");
            emit("br x30");
            return;
        }

        /* __builtin_setjmp(buf) */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_setjmp") == 0) {
            gen_expr(n->args);
            emit("stp x29, x30, [x0]");
            emit("mov x1, sp");
            emit("str x1, [x0, #16]");
            emit("mov x0, #0");
            return;
        }

        /* __builtin_complex(re, im) -> create complex double
         * Result: d0=real, d1=imag */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_complex") == 0) {
            /* generate imag part first, push it */
            gen_expr(n->args->next);
            emit("str d0, [sp, #-16]!");
            /* generate real part into d0 */
            gen_expr(n->args);
            /* pop imag into d1 */
            emit("ldr d1, [sp], #16");
            return;
        }

        /* __builtin_{add,sub,mul}_overflow_p(a, b, type_expr) */
        if (n->name != NULL &&
            (strcmp(n->name, "__builtin_add_overflow_p") == 0 ||
             strcmp(n->name, "__builtin_sub_overflow_p") == 0 ||
             strcmp(n->name, "__builtin_mul_overflow_p") == 0)) {
            struct type *rty;
            int res_unsigned;
            int res_32;
            int op;

            op = 2;
            if (strcmp(n->name, "__builtin_add_overflow_p") == 0) {
                op = 0;
            } else if (strcmp(n->name, "__builtin_sub_overflow_p") == 0) {
                op = 1;
            }

            gen_expr(n->args);
            push();
            gen_expr(n->args->next);
            push();
            pop("x2");
            pop("x1");

            rty = ty_int;
            if (n->args != NULL && n->args->next != NULL &&
                n->args->next->next != NULL &&
                n->args->next->next->ty != NULL) {
                rty = n->args->next->next->ty;
            }
            res_unsigned = (rty != NULL && rty->is_unsigned);
            res_32 = (rty != NULL && rty->size <= 4);

            if (op == 0) {
                if (res_32) {
                    emit("adds w0, w1, w2");
                    emit("cset w0, %s", res_unsigned ? "cs" : "vs");
                } else {
                    emit("adds x0, x1, x2");
                    emit("cset w0, %s", res_unsigned ? "cs" : "vs");
                }
            } else if (op == 1) {
                if (res_32) {
                    emit("subs w0, w1, w2");
                    emit("cset w0, %s", res_unsigned ? "cc" : "vs");
                } else {
                    emit("subs x0, x1, x2");
                    emit("cset w0, %s", res_unsigned ? "cc" : "vs");
                }
            } else {
                if (res_32 && res_unsigned) {
                    emit("umull x0, w1, w2");
                    emit("lsr x4, x0, #32");
                    emit("cmp x4, #0");
                    emit("cset w0, ne");
                } else if (res_32) {
                    emit("smull x0, w1, w2");
                    emit("cmp x0, w0, sxtw");
                    emit("cset w0, ne");
                } else if (res_unsigned) {
                    emit("umulh x4, x1, x2");
                    emit("mul x0, x1, x2");
                    emit("cmp x4, #0");
                    emit("cset w0, ne");
                } else {
                    emit("smulh x4, x1, x2");
                    emit("mul x0, x1, x2");
                    emit("cmp x4, x0, asr #63");
                    emit("cset w0, ne");
                }
            }
            return;
        }

        /* __builtin_abort() -> bl abort (no args) */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_abort") == 0) {
            emit("bl abort");
            return;
        }

        /*
         * Redirect __builtin_* to libc equivalents.
         * Rewrite the name so the normal call path emits bl <func>.
         */
        if (n->name != NULL &&
            strncmp(n->name, "__builtin_", 10) == 0) {
            if (strcmp(n->name, "__builtin_memcpy") == 0)
                n->name = "memcpy";
            else if (strcmp(n->name, "__builtin_memset") == 0)
                n->name = "memset";
            else if (strcmp(n->name, "__builtin_memcmp") == 0)
                n->name = "memcmp";
            else if (strcmp(n->name, "__builtin_memmove") == 0)
                n->name = "memmove";
            else if (strcmp(n->name, "__builtin_strlen") == 0)
                n->name = "strlen";
            else if (strcmp(n->name, "__builtin_strcmp") == 0)
                n->name = "strcmp";
            else if (strcmp(n->name, "__builtin_strcpy") == 0)
                n->name = "strcpy";
            else if (strcmp(n->name, "__builtin_strncpy") == 0)
                n->name = "strncpy";
            else if (strcmp(n->name, "__builtin_printf") == 0)
                n->name = "printf";
            else if (strcmp(n->name, "__builtin_sprintf") == 0)
                n->name = "sprintf";
            else if (strcmp(n->name, "__builtin_malloc") == 0)
                n->name = "malloc";
            else if (strcmp(n->name, "__builtin_alloca") == 0)
                n->name = "alloca";
            else if (strcmp(n->name, "__builtin_free") == 0)
                n->name = "free";
            else if (strcmp(n->name, "__builtin_exit") == 0)
                n->name = "exit";
            else if (strcmp(n->name, "__builtin_puts") == 0)
                n->name = "puts";
            else if (strcmp(n->name, "__builtin_strchr") == 0)
                n->name = "strchr";
            else if (strcmp(n->name, "__builtin_strncpy") == 0)
                n->name = "strncpy";
            else if (strcmp(n->name, "__builtin_snprintf") == 0)
                n->name = "snprintf";
        }

        /* __builtin_nan("") -> quiet NaN double */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_nan") == 0) {
            emit("mov x0, #0");
            emit("movk x0, #0x7FF8, lsl #48");
            emit("fmov d0, x0");
            return;
        }
        /* __builtin_nanf("") -> quiet NaN float */
        if (n->name != NULL &&
            strcmp(n->name, "__builtin_nanf") == 0) {
            emit("mov w0, #0");
            emit("movk w0, #0x7FC0, lsl #16");
            emit("fmov s0, w0");
            return;
        }

        /* __builtin_inf() / __builtin_huge_val() -> +Inf double */
        if (n->name != NULL &&
            (strcmp(n->name, "__builtin_inf") == 0 ||
             strcmp(n->name, "__builtin_huge_val") == 0)) {
            emit("mov x0, #0");
            emit("movk x0, #0x7FF0, lsl #48");
            emit("fmov d0, x0");
            return;
        }
        /* __builtin_inff() / __builtin_huge_valf() -> +Inf float */
        if (n->name != NULL &&
            (strcmp(n->name, "__builtin_inff") == 0 ||
             strcmp(n->name, "__builtin_huge_valf") == 0)) {
            emit("mov w0, #0");
            emit("movk w0, #0x7F80, lsl #16");
            emit("fmov s0, w0");
            return;
        }

        /* count arguments */
        nargs = 0;
        for (arg = n->args; arg; arg = arg->next) {
            nargs++;
        }

        /* Indirect call through function pointer (member access, deref,
         * call returning fptr, cast to fptr, ternary selecting fptr,
         * etc.) -- also catches any call where n->name is NULL */
        if (n->name == NULL ||
            (n->lhs && (n->lhs->kind == ND_MEMBER ||
                       n->lhs->kind == ND_DEREF ||
                       n->lhs->kind == ND_CALL ||
                       n->lhs->kind == ND_CAST ||
                       n->lhs->kind == ND_TERNARY ||
                       (n->lhs->kind == ND_VAR && n->lhs->ty &&
                        n->lhs->ty->kind == TY_PTR &&
                        n->lhs->ty->base &&
                        n->lhs->ty->base->kind == TY_FUNC)))) {
            /* evaluate function pointer expression;
             * gen_expr loads the pointer value into x0 */
            {
            int iarg_is_fp[128];
            int igp_idx, ifp_idx;
            struct type *ifty;

            gen_expr(n->lhs);
            push(); /* save fptr on stack */

            /* get the function type from the pointer type */
            ifty = NULL;
            if (n->lhs && n->lhs->ty) {
                ifty = n->lhs->ty;
                if (ifty->kind == TY_PTR && ifty->base)
                    ifty = ifty->base;
                if (ifty->kind != TY_FUNC)
                    ifty = NULL;
            }

            /* evaluate args, push each to stack;
             * track float vs GP like the direct call path */
            i = 0;
            for (arg = n->args; arg; arg = arg->next) {
                gen_expr(arg);
                if (arg->ty != NULL && type_is_fp128(arg->ty)) {
                    push_q();
                    if (i < 128) {
                        iarg_is_fp[i] = 3;
                    }
                } else if (arg->ty != NULL &&
                           type_is_real_flonum(arg->ty)) {
                    push_fp();
                    if (i < 128) {
                        iarg_is_fp[i] =
                            (arg->ty->kind == TY_FLOAT) ? 2 : 1;
                    }
                } else {
                    push();
                    if (i < 128) iarg_is_fp[i] = 0;
                }
                i++;
            }
            nargs = i;

            /* assign registers: pop in reverse order (LIFO).
             * The last-pushed arg (index nargs-1) is popped first
             * and should go to the highest register index. */
            {
            int gp_count, fp_count;
            gp_count = 0;
            fp_count = 0;
            for (i = 0; i < nargs && i < 128; i++) {
                if (iarg_is_fp[i] >= 1) fp_count++;
                else gp_count++;
            }
            igp_idx = gp_count - 1;
            ifp_idx = fp_count - 1;
            for (i = nargs - 1; i >= 0; i--) {
                if (i < 128 && iarg_is_fp[i] >= 1) {
                    char freg[16];
                    if (iarg_is_fp[i] == 3) {
                        sprintf(freg, "q%d",
                                ifp_idx < 8 ? ifp_idx : 7);
                        pop_q(freg);
                    } else {
                        sprintf(freg, "d%d",
                                ifp_idx < 8 ? ifp_idx : 7);
                        pop_fp(freg);
                    }
                    if (iarg_is_fp[i] == 2) {
                        int ri;
                        ri = ifp_idx < 8 ? ifp_idx : 7;
                        emit("fcvt s%d, d%d", ri, ri);
                    }
                    ifp_idx--;
                } else {
                    pop(arg_regs[igp_idx < 8 ? igp_idx : 7]);
                    igp_idx--;
                }
            }
            }
            pop("x9");
            emit("blr x9");
            }
            return;
        }

        /*
         * Evaluate args, push to stack. Track FP vs GP for each slot.
         * AAPCS64: GP args in x0-x7, FP args in d0-d7 (independent).
         * Variadic float args are passed as raw bits in GP regs.
         */
        {
            struct type *fty;
            int arg_is_fp[128];  /* 0=GP, 1=FP-double, 2=FP-float */
            int arg_span[128];   /* grouped-argument span map */
            char arg_reg[128][8]; /* assigned register name */
            int gp_idx, fp_idx;
            int is_var;
            int named_count;
            int arg_idx;
            int slot;
            int depth_before_args;

            fty = get_func_type(n);
            is_var = (fty != NULL && fty->is_variadic);
            (void)is_var;
            emit_comment("call lhs=%s lhsty=%d n->name=%s", n->lhs ? "yes" : "null", (n->lhs && n->lhs->ty) ? (int)n->lhs->ty->kind : -1, n->name ? n->name : "null");

            /* count named parameters */
            named_count = 0;
            if (fty != NULL) {
                struct type *pt;
                for (pt = fty->params; pt != NULL; pt = pt->next)
                    named_count++;
            }

            /* Save depth before arg evaluation so we can clean up
             * any extra stack space allocated by compound literals
             * or init lists during arg evaluation. */
            depth_before_args = depth;

            /* evaluate and push each arg */
            arg_idx = 0;
            nargs = 0;
            for (arg = n->args; arg; arg = arg->next) {
                int is_named;
                is_named = (arg_idx < named_count);

                if (arg->ty != NULL &&
                    (arg->ty->kind == TY_STRUCT ||
                     arg->ty->kind == TY_UNION)) {
                    int hfa_kind;
                    hfa_kind = is_named ? is_hfa(arg->ty) : 0;

                    if (hfa_kind) {
                        /* HFA: load each member into FP regs */
                        int hcnt, mi, msz;
                        hcnt = hfa_member_count(arg->ty);
                        msz = hfa_kind;
                        gen_expr(arg);
                        emit("mov x9, x0");
                        for (mi = 0; mi < hcnt; mi++) {
                            if (hfa_kind == 4) {
                                emit("ldr s0, [x9, #%d]", mi * msz);
                                emit("fcvt d0, s0");
                                push_fp();
                                if (nargs < 128) {
                                    arg_is_fp[nargs] = 2;
                                    arg_span[nargs] = (mi == 0) ? hcnt : 0;
                                }
                                nargs++;
                            } else if (hfa_kind == 16) {
                                emit("ldr q0, [x9, #%d]", mi * msz);
                                push_q();
                                if (nargs < 128) {
                                    arg_is_fp[nargs] = 3;
                                    arg_span[nargs] = (mi == 0) ? hcnt : 0;
                                }
                                nargs++;
                            } else {
                                emit("ldr d0, [x9, #%d]", mi * msz);
                                push_fp();
                                if (nargs < 128) {
                                    arg_is_fp[nargs] = 1;
                                    arg_span[nargs] = (mi == 0) ? hcnt : 0;
                                }
                                nargs++;
                            }
                        }
                    } else if (arg->kind == ND_CALL ||
                               arg->kind == ND_STMT_EXPR) {
                        /* non-HFA struct from call/stmt_expr:
                         * value is in registers x0(/x1), not
                         * behind a pointer. Push directly. */
                        int asz;
                        gen_expr(arg);
                        asz = arg->ty->size;
                        if (asz <= 8) {
                            push();
                            if (nargs < 128) {
                                arg_is_fp[nargs] = 0;
                                arg_span[nargs] = 1;
                            }
                            nargs++;
                        } else if (asz <= 16) {
                            push();
                            if (nargs < 128) {
                                arg_is_fp[nargs] = 0;
                                arg_span[nargs] = 2;
                            }
                            nargs++;
                            emit("mov x0, x1");
                            push();
                            if (nargs < 128) {
                                arg_is_fp[nargs] = 0;
                                arg_span[nargs] = 0;
                            }
                            nargs++;
                        } else {
                            push();
                            if (nargs < 128) {
                                arg_is_fp[nargs] = 0;
                                arg_span[nargs] = 1;
                            }
                            nargs++;
                        }
                    } else {
                        /* non-HFA struct: pass in GP regs.
                         * gen_expr for struct leaves address
                         * in x0 (arrays/variables/members). */
                        int asz;
                        gen_expr(arg);
                        asz = arg->ty->size;
                        if (asz <= 8) {
                            emit("ldr x0, [x0]");
                            push();
                            if (nargs < 128) {
                                arg_is_fp[nargs] = 0;
                                arg_span[nargs] = 1;
                            }
                            nargs++;
                        } else if (asz <= 16) {
                            emit("ldp x2, x3, [x0]");
                            emit("mov x0, x2");
                            push();
                            if (nargs < 128) {
                                arg_is_fp[nargs] = 0;
                                arg_span[nargs] = 2;
                            }
                            nargs++;
                            emit("mov x0, x3");
                            push();
                            if (nargs < 128) {
                                arg_is_fp[nargs] = 0;
                                arg_span[nargs] = 0;
                            }
                            nargs++;
                        } else {
                            push();
                            if (nargs < 128) {
                                arg_is_fp[nargs] = 0;
                                arg_span[nargs] = 1;
                            }
                            nargs++;
                        }
                    }
                } else if (arg->ty != NULL &&
                           (arg->ty->kind == TY_COMPLEX_DOUBLE ||
                            arg->ty->kind == TY_COMPLEX_FLOAT)) {
                    /* complex: pass as two consecutive FP regs.
                     * gen_expr returns address in x0.
                     * Load both parts before pushing to avoid
                     * sp aliasing issues. */
                    gen_expr(arg);
                    if (arg->ty->kind == TY_COMPLEX_DOUBLE) {
                        emit("ldr d1, [x0, #8]");
                        emit("ldr d0, [x0]");
                        push_fp();
                        if (nargs < 128) {
                            arg_is_fp[nargs] = 1;
                            arg_span[nargs] = 2;
                        }
                        nargs++;
                        emit("fmov d0, d1");
                        push_fp();
                        if (nargs < 128) {
                            arg_is_fp[nargs] = 1;
                            arg_span[nargs] = 0;
                        }
                        nargs++;
                    } else {
                        emit("ldr s1, [x0, #4]");
                        emit("ldr s0, [x0]");
                        emit("fcvt d0, s0");
                        push_fp();
                        if (nargs < 128) {
                            arg_is_fp[nargs] = 2;
                            arg_span[nargs] = 2;
                        }
                        nargs++;
                        emit("fcvt d0, s1");
                        push_fp();
                        if (nargs < 128) {
                            arg_is_fp[nargs] = 2;
                            arg_span[nargs] = 0;
                        }
                        nargs++;
                    }
                } else if (arg->ty != NULL &&
                           type_is_fp128(arg->ty)) {
                    gen_expr(arg);
                    push_q();
                    if (nargs < 128) {
                        arg_is_fp[nargs] = 3;
                        arg_span[nargs] = 1;
                    }
                    nargs++;
                } else if (arg->ty != NULL &&
                           type_is_real_flonum(arg->ty)) {
                    gen_expr(arg);
                    /* AAPCS64 (GNU/Linux): all float/double args
                     * including variadic are passed in FP regs
                     * d0-d7. Variadic floats are promoted to
                     * double (default argument promotion). */
                    push_fp();
                    if (nargs < 128) {
                        if (is_var && !is_named &&
                            arg->ty->kind == TY_FLOAT) {
                            /* variadic float -> double promotion */
                            arg_is_fp[nargs] = 1;
                        } else {
                            arg_is_fp[nargs] =
                                (arg->ty->kind == TY_FLOAT) ? 2 : 1;
                        }
                        arg_span[nargs] = 1;
                    }
                    nargs++;
                } else {
                    gen_expr(arg);
                    push();
                    if (nargs < 128) {
                        arg_is_fp[nargs] = 0;
                        arg_span[nargs] = 1;
                    }
                    nargs++;
                }
                arg_idx++;
            }

            /* assign registers: walk forward, GP and FP independently.
             * Stack-passed args are copied into a call frame below the
             * push-stack. */
            gp_idx = 0;
            fp_idx = 0;
            {
                int push_size;
                int stack_sz;

                /* Assign registers atomically for multi-slot arguments
                 * (HFAs, complex, and 9-16 byte structs). If there
                 * aren't enough registers left for the whole group,
                 * pass the whole group on the stack. */
                slot = 0;
                while (slot < nargs && slot < 128) {
                    int span;
                    int gi;

                    span = arg_span[slot];
                    if (span <= 0) span = 1;

                    if (span == 1) {
                        if (arg_is_fp[slot] >= 1) {
                            if (fp_idx < 8) {
                                if (arg_is_fp[slot] == 3) {
                                    sprintf(arg_reg[slot], "q%d", fp_idx);
                                } else {
                                    sprintf(arg_reg[slot], "d%d", fp_idx);
                                }
                            } else {
                                arg_reg[slot][0] = '\0'; /* stack */
                            }
                            fp_idx++;
                        } else {
                            if (gp_idx < 8) {
                                sprintf(arg_reg[slot], "x%d", gp_idx);
                            } else {
                                arg_reg[slot][0] = '\0'; /* stack */
                            }
                            gp_idx++;
                        }
                        slot++;
                        continue;
                    }

                    if (arg_is_fp[slot] >= 1) {
                        if (fp_idx + span <= 8) {
                            for (gi = 0; gi < span; gi++) {
                                if (arg_is_fp[slot] == 3) {
                                    sprintf(arg_reg[slot + gi], "q%d",
                                            fp_idx + gi);
                                } else {
                                    sprintf(arg_reg[slot + gi], "d%d",
                                            fp_idx + gi);
                                }
                            }
                            fp_idx += span;
                        } else {
                            for (gi = 0; gi < span; gi++) {
                                arg_reg[slot + gi][0] = '\0';
                            }
                        }
                    } else {
                        if (gp_idx + span <= 8) {
                            for (gi = 0; gi < span; gi++) {
                                sprintf(arg_reg[slot + gi], "x%d",
                                        gp_idx + gi);
                            }
                            gp_idx += span;
                        } else {
                            for (gi = 0; gi < span; gi++) {
                                arg_reg[slot + gi][0] = '\0';
                            }
                        }
                    }
                    slot += span;
                }

                /* compute stack argument area size with ABI alignment */
                stack_sz = 0;
                for (slot = 0; slot < nargs && slot < 128; slot++) {
                    if (arg_reg[slot][0] == '\0') {
                        int asz;
                        int aal;
                        asz = (arg_is_fp[slot] == 3) ? 16 : 8;
                        aal = asz;
                        stack_sz = align_to(stack_sz, aal);
                        stack_sz += asz;
                    }
                }
                push_size = align_to(stack_sz, 16);

                if (push_size > 0) {
                    /* Stack-passed args with push-stack.
                     * Push-stack layout (each 16 bytes):
                     *   sp+(nargs-1-slot)*16 = arg[slot]
                     * Load register args by offset, copy
                     * stack args to a call frame below. */
                    int soff;

                    /* load register args from push-stack
                     * offsets (before any sp changes) */
                    for (slot = 0; slot < nargs && slot < 128;
                         slot++) {
                        if (arg_reg[slot][0] != '\0') {
                            int src_off;
                            src_off = (nargs - 1 - slot) * 16;
                            if (arg_is_fp[slot] >= 1) {
                                emit("ldr %s, [sp, #%d]",
                                     arg_reg[slot], src_off);
                                if (arg_is_fp[slot] == 2) {
                                    int ridx;
                                    ridx = arg_reg[slot][1] - '0';
                                    emit("fcvt s%d, d%d",
                                         ridx, ridx);
                                }
                            } else {
                                emit("ldr %s, [sp, #%d]",
                                     arg_reg[slot], src_off);
                            }
                        }
                    }

                    /* allocate call frame for stack args */
                    emit("sub sp, sp, #%d", push_size);

                    /* copy stack-passed args from push-stack
                     * to call frame */
                    soff = 0;
                    for (slot = 0; slot < nargs && slot < 128;
                         slot++) {
                        if (arg_reg[slot][0] == '\0') {
                            int src_off;
                            int asz;
                            int aal;
                            asz = (arg_is_fp[slot] == 3) ? 16 : 8;
                            aal = asz;
                            soff = align_to(soff, aal);
                            src_off = push_size +
                                (nargs - 1 - slot) * 16;
                            if (arg_is_fp[slot] == 3) {
                                emit("ldr q10, [sp, #%d]", src_off);
                                emit("str q10, [sp, #%d]", soff);
                            } else {
                                emit("ldr x10, [sp, #%d]", src_off);
                                emit("str x10, [sp, #%d]", soff);
                            }
                            soff += asz;
                        }
                    }

                    /* set x8 for large struct return */
                    if (!x8_preset && n->ty &&
                        (n->ty->kind == TY_STRUCT ||
                         n->ty->kind == TY_UNION) &&
                        n->ty->size > 16 &&
                        !is_hfa(n->ty)) {
                        int rsz;
                        rsz = align_to(n->ty->size, 16);
                        emit("sub sp, sp, #%d", rsz);
                        emit("mov x8, sp");
                    }

                    /* pass static link for nested function calls.
                     * If caller is itself nested, forward x19 (the
                     * enclosing fp received from our caller).
                     * Otherwise pass our own fp (x29). */
                    if (n->name && strncmp(n->name, ".Lnested.", 9) == 0) {
                        if (current_func &&
                            strncmp(current_func, ".Lnested.", 9) == 0) {
                            /* already nested: x19 holds enclosing fp */
                        } else {
                            emit("mov x19, x29");
                        }
                    }
                    emit("bl %s", n->name);

                    /* reclaim large struct buffer for standalone calls */
                    if (!x8_preset && n->ty &&
                        (n->ty->kind == TY_STRUCT ||
                         n->ty->kind == TY_UNION) &&
                        n->ty->size > 16 &&
                        !is_hfa(n->ty)) {
                        int rsz;
                        rsz = align_to(n->ty->size, 16);
                        emit("add sp, sp, #%d", rsz);
                    }

                    /* reclaim call frame + push-stack */
                    emit("add sp, sp, #%d",
                         push_size + nargs * 16);
                    depth -= nargs;
                } else {
                    /* all args fit in registers */
                    for (i = nargs - 1; i >= 0; i--) {
                        if (i < 128 && arg_is_fp[i] == 3) {
                            pop_q(arg_reg[i]);
                        } else if (i < 128 && arg_is_fp[i] >= 1) {
                            pop_fp(arg_reg[i]);
                            /* float args: convert d-reg to s-reg
                             * for AAPCS64 float calling convention */
                            if (arg_is_fp[i] == 2) {
                                int ridx;
                                ridx = arg_reg[i][1] - '0';
                                emit("fcvt s%d, d%d", ridx, ridx);
                            }
                        } else if (i < 128) {
                            pop(arg_reg[i]);
                        } else {
                            pop("x7");
                        }
                    }

                    /* set x8 for large struct return */
                    if (!x8_preset && n->ty &&
                        (n->ty->kind == TY_STRUCT ||
                         n->ty->kind == TY_UNION) &&
                        n->ty->size > 16 &&
                        !is_hfa(n->ty)) {
                        int rsz;
                        rsz = align_to(n->ty->size, 16);
                        emit("sub sp, sp, #%d", rsz);
                        emit("mov x8, sp");
                    }

                    /* pass static link for nested function calls */
                    if (n->name && strncmp(n->name, ".Lnested.", 9) == 0) {
                        if (current_func &&
                            strncmp(current_func, ".Lnested.", 9) == 0) {
                            /* already nested: x19 holds enclosing fp */
                        } else {
                            emit("mov x19, x29");
                        }
                    }
                    emit("bl %s", n->name);

                    /* reclaim large struct buffer for standalone */
                    if (!x8_preset && n->ty &&
                        (n->ty->kind == TY_STRUCT ||
                         n->ty->kind == TY_UNION) &&
                        n->ty->size > 16 &&
                        !is_hfa(n->ty)) {
                        int rsz;
                        rsz = align_to(n->ty->size, 16);
                        emit("add sp, sp, #%d", rsz);
                    }
                }
            }
            /* Clean up any extra stack space allocated during arg
             * evaluation (e.g. compound literals, init lists).
             * depth should return to depth_before_args. */
            if (depth > depth_before_args) {
                int extra;
                extra = (depth - depth_before_args) * 16;
                emit("add sp, sp, #%d", extra);
                depth = depth_before_args;
            }
        }
        return;

    case ND_COMMA_EXPR:
        gen_expr(n->lhs);
        gen_expr(n->rhs);
        return;

    case ND_CAST:
        gen_expr(n->lhs);
        /* truncate or extend as needed */
        if (n->ty != NULL) {
            int from_fp, to_fp;
            int from_fp128, to_fp128;

            from_fp = (n->lhs != NULL && n->lhs->ty != NULL &&
                       type_is_real_flonum(n->lhs->ty));
            to_fp = type_is_real_flonum(n->ty);
            from_fp128 = (n->lhs != NULL && n->lhs->ty != NULL &&
                          type_is_fp128(n->lhs->ty));
            to_fp128 = type_is_fp128(n->ty);

            /* cast to long double (fp128) */
            if (to_fp128) {
                if (from_fp128) {
                    return;
                }
                if (from_fp) {
                    emit_double_to_fp128();
                    return;
                }
                /* integer -> fp128 (exact) */
                emit_int_to_fp128(n->lhs != NULL ? n->lhs->ty : NULL);
                return;
            }

            /* cast from long double (fp128) */
            if (from_fp128) {
                if (to_fp) {
                    if (n->ty != NULL && n->ty->kind == TY_FLOAT) {
                        emit_fp128_to_float_in_d0();
                    } else {
                        emit_fp128_to_double();
                    }
                    return;
                }
                if (n->ty != NULL && n->ty->kind == TY_BOOL) {
                    emit_fp128_is_nonzero();
                    return;
                }
                /* fp128 -> integer (width-specific helper) */
                if (n->ty != NULL && n->ty->size <= 4) {
                    if (n->ty->is_unsigned) {
                        emit("bl __fixunstfsi");
                    } else {
                        emit("bl __fixtfsi");
                    }
                } else {
                    if (n->ty != NULL && n->ty->is_unsigned) {
                        emit("bl __fixunstfdi");
                    } else {
                        emit("bl __fixtfdi");
                    }
                }
                /* fall through to truncation logic below */
            }

            /* float/double -> integer */
            if (from_fp && !to_fp) {
                if (n->ty->size <= 4) {
                    /* 32-bit target: use w0 to get proper
                     * saturation on overflow (INT_MIN/INT_MAX) */
                    if (n->ty->is_unsigned)
                        emit("fcvtzu w0, d0");
                    else
                        emit("fcvtzs w0, d0");
                } else {
                    if (n->ty->is_unsigned)
                        emit("fcvtzu x0, d0");
                    else
                        emit("fcvtzs x0, d0");
                }
                return;
            }
            /* integer -> float/double */
            if (!from_fp && to_fp) {
                int src_unsigned;
                int src64;
                const char *src_r;
                src_unsigned = (n->lhs->ty != NULL &&
                                n->lhs->ty->is_unsigned);
                src64 = (n->lhs->ty != NULL &&
                         n->lhs->ty->size > 4);
                src_r = src64 ? "x0" : "w0";
                if (src_unsigned) {
                    emit("ucvtf d0, %s", src_r);
                } else {
                    emit("scvtf d0, %s", src_r);
                }
                /* result is always in d0 per our convention */
                return;
            }
            /* float <-> double */
            if (from_fp && to_fp) {
                if (n->ty->kind == TY_FLOAT &&
                    n->lhs->ty->kind == TY_DOUBLE) {
                    /* truncate double to float precision, keep in d0 */
                    emit("fcvt s0, d0");
                    emit("fcvt d0, s0");
                } else if (n->ty->kind == TY_DOUBLE &&
                           n->lhs->ty->kind == TY_FLOAT) {
                    /* float already promoted to d0 by emit_load;
                     * no conversion needed */
                }
                return;
            }

            /* _Bool conversion: nonzero -> 1, zero -> 0 */
            if (n->ty->kind == TY_BOOL) {
                if (from_fp) {
                    emit("fcmp d0, #0.0");
                    emit("cset x0, ne");
                } else {
                    emit("cmp x0, #0");
                    emit("cset x0, ne");
                }
                return;
            }
            /* Skip integer truncation/extension for pointer,
             * array, function, struct, and union types. */
            if (n->ty->kind == TY_PTR || n->ty->kind == TY_ARRAY ||
                n->ty->kind == TY_FUNC || n->ty->kind == TY_STRUCT ||
                n->ty->kind == TY_UNION) {
                return;
            }
            /* Also skip if source is pointer/array/func/struct/union --
             * don't truncate a 64-bit address to 32-bit. */
            if (n->lhs && n->lhs->ty &&
                (n->lhs->ty->kind == TY_PTR ||
                 n->lhs->ty->kind == TY_ARRAY ||
                 n->lhs->ty->kind == TY_FUNC ||
                 n->lhs->ty->kind == TY_STRUCT ||
                 n->lhs->ty->kind == TY_UNION)) {
                return;
            }
            sz = n->ty->size;
            if (sz == 1) {
                if (n->ty->is_unsigned) {
                    emit("and x0, x0, #0xff");
                } else {
                    emit("sxtb x0, w0");
                }
            } else if (sz == 2) {
                if (n->ty->is_unsigned) {
                    emit("and x0, x0, #0xffff");
                } else {
                    emit("sxth x0, w0");
                }
            } else if (sz == 4) {
                if (n->ty->is_unsigned) {
                    emit("mov w0, w0");
                } else {
                    emit("sxtw x0, w0");
                }
            } else if (sz == 8 && n->lhs != NULL &&
                       n->lhs->ty != NULL &&
                       n->lhs->ty->size <= 4) {
                /* widening cast from <=32-bit to 64-bit */
                if (n->lhs->ty->is_unsigned) {
                    emit("mov w0, w0");
                } else {
                    emit("sxtw x0, w0");
                }
            }
        }
        return;

    /* C99: _Bool conversion */
    case ND_BOOL_CONV:
        gen_expr(n->lhs);
        emit_truth_test(n->lhs);
        return;

    /* C99: VLA allocation */
    case ND_VLA_ALLOC:
        if (n->vla_size) {
            gen_expr(n->vla_size);
        }
        /* multiply by element size */
        if (n->ty && n->ty->base) {
            sz = n->ty->base->size;
            if (sz > 1) {
                emit("mov x1, #%d", sz);
                emit("mul x0, x0, x1");
            }
        }
        /* align to 16 */
        emit("add x0, x0, #15");
        emit("and x0, x0, #-16");
        /* allocate on stack */
        emit("sub sp, sp, x0");
        emit("mov x0, sp");
        return;

    /* C99: compound literal */
    case ND_COMPOUND_LIT:
    {
        /* Use pre-allocated frame space (offset assigned by
         * assign_compound_lit_offsets in gen_funcdef). */
        int clsz, ci2;
        struct node *elem;

        clsz = (n->ty != NULL) ? n->ty->size : 8;
        clsz = align_to(clsz, 16);
        if (n->offset > 0) {
            /* Use frame-pointer-relative addressing for the
             * pre-allocated compound literal slot. */
            if (current_is_leaf) {
                int sp_off;
                sp_off = current_stack_size + depth * 16 - n->offset;
                emit("add x9, sp, #%d", sp_off);
            } else {
                emit_sub_fp("x9", n->offset);
            }
        } else {
            /* Fallback: dynamic stack allocation (should not
             * normally happen if assign_compound_lit_offsets ran). */
            emit("sub sp, sp, #%d", clsz);
            depth += clsz / 16;
            emit("mov x9, sp");
        }
        /* zero fill */
        {
            int base2;
            int step2;
            base2 = 0;
            for (ci2 = 0; ci2 + 8 <= clsz; ci2 += 8) {
                if (ci2 - base2 > 32752) {
                    step2 = ci2 - base2;
                    if (step2 <= 4095) {
                        emit("add x9, x9, #%d", step2);
                    } else {
                        emit_load_imm_reg("x10", step2);
                        emit("add x9, x9, x10");
                    }
                    base2 = ci2;
                }
                emit("str xzr, [x9, #%d]", ci2 - base2);
            }
            for (; ci2 < clsz; ci2++) {
                if (ci2 - base2 > 4095) {
                    step2 = ci2 - base2;
                    if (step2 <= 4095) {
                        emit("add x9, x9, #%d", step2);
                    } else {
                        emit_load_imm_reg("x10", step2);
                        emit("add x9, x9, x10");
                    }
                    base2 = ci2;
                }
                emit("strb wzr, [x9, #%d]", ci2 - base2);
            }
        }
        /* initialize from body init list */
        if (n->body != NULL && n->body->kind == ND_INIT_LIST &&
            n->ty != NULL) {
            if (n->ty->kind == TY_STRUCT || n->ty->kind == TY_UNION ||
                (n->ty->kind == TY_ARRAY && n->ty->base)) {
                emit_cl_init_struct(n->body, n->ty, 0, n->offset);
            } else {
                /* scalar compound literal: (int){1}, (float){2.0}, etc.
                 * Store the first element's value into the allocated slot. */
                elem = n->body->body;
                if (elem != NULL) {
                    struct node *val;
                    val = (elem->kind == ND_DESIG_INIT) ?
                          elem->lhs : elem;
                    if (val != NULL) {
                        gen_expr(val);
                        if (n->offset > 0) {
                            if (current_is_leaf) {
                                int sp_off2;
                                sp_off2 = current_stack_size
                                         + depth * 16 - n->offset;
                                emit("add x9, sp, #%d", sp_off2);
                            } else {
                                emit_sub_fp("x9", n->offset);
                            }
                        } else {
                            emit("mov x9, sp");
                        }
                        if (type_is_fp128(n->ty)) {
                            emit("str q0, [x9]");
                        } else if (type_is_flonum(n->ty)) {
                            if (n->ty->kind == TY_FLOAT) {
                                emit("fcvt s0, d0");
                                emit("str s0, [x9]");
                            } else {
                                emit("str d0, [x9]");
                            }
                        } else if (n->ty->size == 1) {
                            emit("strb w0, [x9]");
                        } else if (n->ty->size == 2) {
                            emit("strh w0, [x9]");
                        } else if (n->ty->size == 4) {
                            emit("str w0, [x9]");
                        } else {
                            emit("str x0, [x9]");
                        }
                    }
                }
            }
        }
        if (n->offset > 0) {
            if (current_is_leaf) {
                int sp_off2;
                sp_off2 = current_stack_size + depth * 16 - n->offset;
                emit("add x0, sp, #%d", sp_off2);
            } else {
                emit_sub_fp("x0", n->offset);
            }
        } else {
            emit("mov x0, sp");
        }
        /* For scalar/pointer compound literals used as rvalues,
         * load the value. Struct/union/array stay as addresses. */
        if (n->ty != NULL &&
            n->ty->kind != TY_STRUCT && n->ty->kind != TY_UNION &&
            n->ty->kind != TY_ARRAY) {
            emit_load(n->ty);
        }
        return;
    }

    /* init list as expression: allocate on stack, zero, fill.
     * This handles cases where the parser could not expand the
     * init list into per-member assignments. */
    case ND_INIT_LIST:
    {
        int ilsz, ci2;

        ilsz = (n->ty != NULL) ? n->ty->size : 8;
        ilsz = align_to(ilsz, 16);
        emit_comment("init_list expr (%d bytes)", ilsz);
        if (n->offset > 0) {
            emit_sub_fp("x9", n->offset);
        } else {
            emit("sub sp, sp, #%d", ilsz);
            depth += ilsz / 16;
            emit("mov x9, sp");
        }
        /* zero fill */
        {
            int base2;
            int step2;
            base2 = 0;
            for (ci2 = 0; ci2 + 8 <= ilsz; ci2 += 8) {
                if (ci2 - base2 > 32752) {
                    step2 = ci2 - base2;
                    if (step2 <= 4095) {
                        emit("add x9, x9, #%d", step2);
                    } else {
                        emit_load_imm_reg("x10", step2);
                        emit("add x9, x9, x10");
                    }
                    base2 = ci2;
                }
                emit("str xzr, [x9, #%d]", ci2 - base2);
            }
            for (; ci2 < ilsz; ci2++) {
                if (ci2 - base2 > 4095) {
                    step2 = ci2 - base2;
                    if (step2 <= 4095) {
                        emit("add x9, x9, #%d", step2);
                    } else {
                        emit_load_imm_reg("x10", step2);
                        emit("add x9, x9, x10");
                    }
                    base2 = ci2;
                }
                emit("strb wzr, [x9, #%d]", ci2 - base2);
            }
        }
        /* store each element using recursive initializer */
        if (n->ty != NULL &&
            (n->ty->kind == TY_STRUCT || n->ty->kind == TY_UNION ||
             (n->ty->kind == TY_ARRAY && n->ty->base != NULL))) {
            /* wrap n->body in a fake init list node for the helper;
             * ND_INIT_LIST uses body directly, while compound lit
             * uses body->body.  Build a temporary wrapper. */
            struct node fake_il;
            memset(&fake_il, 0, sizeof(fake_il));
            fake_il.kind = ND_INIT_LIST;
            fake_il.body = n->body;
            emit_cl_init_struct(&fake_il, n->ty, 0, n->offset);
        }
        if (n->offset > 0) {
            emit_sub_fp("x0", n->offset);
        } else {
            emit("mov x0, sp");
        }
        return;
    }

    /* designated init as expression: just emit the value */
    case ND_DESIG_INIT:
        if (n->lhs != NULL) {
            gen_expr(n->lhs);
        } else {
            emit("mov x0, #0");
        }
        return;

    /* C23: nullptr */
    case ND_NULLPTR:
        emit("mov x0, #0");
        return;

    /* C11: static assert (no-op at codegen) */
    case ND_STATIC_ASSERT:
        return;

    /* Inline asm is a statement.  If it reaches expression codegen,
     * emit it and clear x0 so statement-expr tails stay well-defined. */
    case ND_GCC_ASM:
        gen_stmt(n);
        emit("mov x0, #0");
        return;

    /* GNU statement expression: ({ stmt; stmt; expr; })
     * Emit all statements; the last one leaves its value in x0. */
    case ND_STMT_EXPR:
    {
        struct node *cur;
        struct node *last;

        emit_comment("stmt_expr begin");
        /* find the last statement */
        last = NULL;
        for (cur = n->body; cur != NULL; cur = cur->next) {
            last = cur;
        }
        /* emit all statements except the last as statements */
        for (cur = n->body; cur != NULL; cur = cur->next) {
            if (cur->next != NULL) {
                gen_stmt(cur);
            }
        }
        /* emit the last node as an expression so its value
         * remains in x0.  If the last node is a pure statement
         * (if/while/for/do/switch/goto/break/continue/return),
         * emit it as a statement and leave x0 = 0. */
        if (last != NULL) {
            switch (last->kind) {
            case ND_IF:
            case ND_WHILE:
            case ND_FOR:
            case ND_DO:
            case ND_SWITCH:
            case ND_GOTO:
            case ND_GOTO_INDIRECT:
            case ND_BREAK:
            case ND_CONTINUE:
            case ND_RETURN:
            case ND_BLOCK:
                gen_stmt(last);
                emit("mov x0, #0");
                break;
            default:
                gen_expr(last);
                break;
            }
        }
        emit_comment("stmt_expr end");
        return;
    }

    /*
     * ND_VA_START - initialize a va_list struct.
     *
     * aarch64 va_list layout (32 bytes):
     *   [+0]  __stack    = pointer to first stack-passed variadic arg
     *   [+8]  __gr_top   = pointer past end of GP save area
     *  [+16]  __vr_top   = pointer past end of FP save area (unused)
     *  [+24]  __gr_offs  = -(8 - named_gp) * 8
     *  [+28]  __vr_offs  = 0
     *
     * The GP save area holds x0..x7 at [fp - va_save_offset] down.
     * gr_top points past the last slot: fp - va_save_offset + 64.
     * The first unnamed arg is at gr_top + gr_offs.
     */
    case ND_VA_START:
    {
        int gr_offs_val;

        emit_comment("va_start");
        /* compute address of va_list struct -> x9 */
        gen_addr(n->lhs);
        emit("mov x9, x0");

        /* __stack = fp + va_stack_start  (skip named stack args) */
        emit("add x0, x29, #%d", va_stack_start);
        emit("str x0, [x9]");

        /* __gr_top = fp - va_save_offset + 64 */
        emit_sub_fp("x0", va_save_offset - 64);
        emit("str x0, [x9, #8]");

        /* __vr_top = fp - va_fp_save_offset + 128 */
        if (va_fp_save_offset > 0) {
            emit_sub_fp("x0", va_fp_save_offset - 128);
            emit("str x0, [x9, #16]");
        } else {
            emit("str xzr, [x9, #16]");
        }

        /* __gr_offs = -(8 - min(named_gp, 8)) * 8 */
        gr_offs_val = -((8 - (va_named_gp > 8 ? 8 : va_named_gp))
                        * 8);
        if (gr_offs_val == 0) {
            emit("str wzr, [x9, #24]");
        } else {
            emit("mov w0, #%d", gr_offs_val);
            emit("str w0, [x9, #24]");
        }

        /* __vr_offs = -(8 - min(named_fp, 8)) * 16 */
        {
            int vr_offs_val;
            vr_offs_val = -((8 - (va_named_fp > 8 ? 8 : va_named_fp))
                            * 16);
            if (vr_offs_val == 0) {
                emit("str wzr, [x9, #28]");
            } else {
                emit("mov w0, #%d", vr_offs_val);
                emit("str w0, [x9, #28]");
            }
        }
        return;
    }

    /*
     * ND_VA_ARG - fetch the next argument from a va_list.
     *
     * For integer/pointer types (all GP-class on aarch64):
     *   1. Load gr_offs from [ap + 24]
     *   2. If gr_offs >= 0, all reg args consumed -> use __stack
     *   3. Else: load from gr_top + gr_offs, advance gr_offs by 8
     *   4. Stack fallback: load from __stack, advance __stack by 8
     */
    case ND_VA_ARG:
    {
        int lbl_stack;
        int lbl_done;
        int is_fp_arg;

        lbl_stack = new_label();
        lbl_done = new_label();
        is_fp_arg = (n->ty != NULL && type_is_fp_scalar(n->ty));

        emit_comment("va_arg");
        /* compute address of va_list -> x9 */
        gen_addr(n->lhs);
        emit("mov x9, x0");

        if (is_fp_arg) {
            /* FP path: use vr_offs [ap+28] and vr_top [ap+16] */
            emit("ldrsw x10, [x9, #28]");
            emit("cmp x10, #0");
            emit("b.ge .L.va_stk.%d", lbl_stack);
            /* register path: load from vr_top + vr_offs */
            emit("ldr x11, [x9, #16]");
            emit("add x12, x11, x10");
            if (n->ty != NULL && type_is_fp128(n->ty)) {
                emit("ldr q0, [x12]");
            } else {
                emit("ldr d0, [x12]");
            }
            /* advance vr_offs by 16 (each VR slot is 16 bytes) */
            emit("add w10, w10, #16");
            emit("str w10, [x9, #28]");
            emit("b .L.va_done.%d", lbl_done);
            /* stack path */
            emit_label(".L.va_stk.%d", lbl_stack);
            emit("ldr x12, [x9]");
            if (n->ty != NULL && type_is_fp128(n->ty)) {
                emit("ldr q0, [x12]");
                emit("add x12, x12, #16");
            } else {
                emit("ldr d0, [x12]");
                emit("add x12, x12, #8");
            }
            emit("str x12, [x9]");
        } else {
            /* GP path: use gr_offs [ap+24] and gr_top [ap+8] */
            int va_slot_sz;
            int is_two_reg;
            va_slot_sz = 8;
            is_two_reg = 0;
            if (n->ty != NULL &&
                (n->ty->kind == TY_STRUCT ||
                 n->ty->kind == TY_UNION) &&
                n->ty->size > 8 && n->ty->size <= 16) {
                va_slot_sz = 16;
                is_two_reg = 1;
            }
            emit("ldrsw x10, [x9, #24]");
            if (is_two_reg) {
                /* for 2-register structs, need 2 consecutive
                 * reg slots available (gr_offs <= -16) */
                emit("cmn x10, #%d", va_slot_sz);
                emit("b.gt .L.va_stk.%d", lbl_stack);
            } else {
                emit("cmp x10, #0");
                emit("b.ge .L.va_stk.%d", lbl_stack);
            }
            /* register path: load from gr_top + gr_offs */
            emit("ldr x11, [x9, #8]");
            emit("add x12, x11, x10");
            emit("ldr x0, [x12]");
            if (is_two_reg) {
                emit("ldr x1, [x12, #8]");
            }
            /* advance gr_offs */
            emit("add w10, w10, #%d", va_slot_sz);
            emit("str w10, [x9, #24]");
            emit("b .L.va_done.%d", lbl_done);
            /* stack path */
            emit_label(".L.va_stk.%d", lbl_stack);
            emit("ldr x12, [x9]");
            emit("ldr x0, [x12]");
            if (is_two_reg) {
                emit("ldr x1, [x12, #8]");
            }
            emit("add x12, x12, #%d", va_slot_sz);
            emit("str x12, [x9]");
        }

        emit_label(".L.va_done.%d", lbl_done);

        /* truncate result to the requested type */
        if (!is_fp_arg && n->ty != NULL) {
            sz = n->ty->size;
            if (sz == 1) {
                if (n->ty->is_unsigned) {
                    emit("and x0, x0, #0xff");
                } else {
                    emit("sxtb x0, w0");
                }
            } else if (sz == 2) {
                if (n->ty->is_unsigned) {
                    emit("and x0, x0, #0xffff");
                } else {
                    emit("sxth x0, w0");
                }
            } else if (sz == 4) {
                if (n->ty->is_unsigned) {
                    emit("mov w0, w0");
                } else {
                    emit("sxtw x0, w0");
                }
            }
        }
        return;
    }

    case ND_TERNARY:
        lbl = new_label();
        gen_expr(n->cond);
        emit_truth_test(n->cond);
        emit("cmp x0, #0");
        emit("b.eq .L.else.%d", lbl);
        gen_expr(n->then_);
        emit("b .L.end.%d", lbl);
        emit_label(".L.else.%d", lbl);
        gen_expr(n->els);
        emit_label(".L.end.%d", lbl);
        return;

    case ND_PRE_INC:
        sz = inc_dec_step(n->lhs);
        gen_addr(n->lhs);
        push();
        emit("mov x1, x0");
        if (n->lhs->kind == ND_MEMBER && n->lhs->bit_width > 0) {
            emit_bitfield_load(n->lhs);
        } else {
            emit_load(n->lhs->ty);
        }
        if (n->lhs->ty && type_is_fp128(n->lhs->ty)) {
            /* fp128 increment: q0 = q0 + 1.0L via libcall */
            push_q(); /* save old across helper call */
            emit("fmov d0, #1.0");
            emit_double_to_fp128();        /* q0 = 1.0L */
            emit("mov v1.16b, v0.16b");    /* q1 = 1.0L */
            pop_q("q0");                   /* q0 = old */
            emit("bl __addtf3");           /* q0 = old + 1.0L */
        } else if (n->lhs->ty && type_is_flonum(n->lhs->ty)) {
            emit("fmov d1, #1.0");
            emit("fadd d0, d0, d1");
        } else {
            emit("add x0, x0, #%d", sz);
        }
        /* truncate to operand type for correct value */
        if (n->lhs->ty && n->lhs->ty->size == 1) {
            if (n->lhs->ty->is_unsigned)
                emit("and x0, x0, #0xff");
            else emit("sxtb x0, w0");
        } else if (n->lhs->ty && n->lhs->ty->size == 2) {
            if (n->lhs->ty->is_unsigned)
                emit("and x0, x0, #0xffff");
            else emit("sxth x0, w0");
        }
        pop("x1");
        if (n->lhs->kind == ND_MEMBER && n->lhs->bit_width > 0) {
            emit_bitfield_store(n->lhs);
        } else {
            emit_store(n->lhs->ty);
        }
        return;

    case ND_PRE_DEC:
        sz = inc_dec_step(n->lhs);
        gen_addr(n->lhs);
        push();
        emit("mov x1, x0");
        if (n->lhs->kind == ND_MEMBER && n->lhs->bit_width > 0) {
            emit_bitfield_load(n->lhs);
        } else {
            emit_load(n->lhs->ty);
        }
        if (n->lhs->ty && type_is_fp128(n->lhs->ty)) {
            /* fp128 decrement: q0 = q0 - 1.0L via libcall */
            push_q();
            emit("fmov d0, #1.0");
            emit_double_to_fp128();        /* q0 = 1.0L */
            emit("mov v1.16b, v0.16b");    /* q1 = 1.0L */
            pop_q("q0");                   /* q0 = old */
            emit("bl __subtf3");           /* q0 = old - 1.0L */
        } else if (n->lhs->ty && type_is_flonum(n->lhs->ty)) {
            emit("fmov d1, #1.0");
            emit("fsub d0, d0, d1");
        } else {
            emit("sub x0, x0, #%d", sz);
        }
        /* truncate to operand type for correct value */
        if (n->lhs->ty && n->lhs->ty->size == 1) {
            if (n->lhs->ty->is_unsigned)
                emit("and x0, x0, #0xff");
            else emit("sxtb x0, w0");
        } else if (n->lhs->ty && n->lhs->ty->size == 2) {
            if (n->lhs->ty->is_unsigned)
                emit("and x0, x0, #0xffff");
            else emit("sxth x0, w0");
        }
        pop("x1");
        if (n->lhs->kind == ND_MEMBER && n->lhs->bit_width > 0) {
            emit_bitfield_store(n->lhs);
        } else {
            emit_store(n->lhs->ty);
        }
        return;

    case ND_POST_INC:
        sz = inc_dec_step(n->lhs);
        if (n->lhs->ty && type_is_fp128(n->lhs->ty)) {
            gen_addr(n->lhs);
            push();                        /* push address */
            emit("mov x1, x0");
            emit_load(n->lhs->ty);         /* q0 = old */
            push_q();                      /* save old (result) */
            emit("fmov d0, #1.0");
            emit_double_to_fp128();        /* q0 = 1.0L */
            emit("mov v1.16b, v0.16b");    /* q1 = 1.0L */
            emit("ldr q0, [sp]");          /* q0 = old */
            emit("bl __addtf3");           /* q0 = new */
            emit("ldr x1, [sp, #16]");     /* x1 = address */
            emit_store(n->lhs->ty);        /* store new */
            emit("ldr q0, [sp]");          /* restore old */
            emit("add sp, sp, #32");       /* discard old + address */
            depth -= 2;
        } else if (n->lhs->ty && type_is_flonum(n->lhs->ty)) {
            gen_addr(n->lhs);
            push();
            emit("mov x1, x0");
            emit_load(n->lhs->ty);
            emit("fmov d1, d0");
            emit("fmov d2, #1.0");
            emit("fadd d0, d0, d2");
            pop("x1");
            emit_store(n->lhs->ty);
            emit("fmov d0, d1");
        } else {
            gen_addr(n->lhs);
            push();                        /* push address */
            emit("mov x1, x0");
            if (n->lhs->kind == ND_MEMBER && n->lhs->bit_width > 0) {
                emit_bitfield_load(n->lhs);
            } else {
                emit_load(n->lhs->ty);
            }
            push();                        /* push original value */
            emit("add x0, x0, #%d", sz);
            emit("ldr x1, [sp, #16]");    /* reload address */
            if (n->lhs->kind == ND_MEMBER && n->lhs->bit_width > 0) {
                emit_bitfield_store(n->lhs);
            } else {
                emit_store(n->lhs->ty);
            }
            pop("x0");                     /* restore original value */
            emit("add sp, sp, #16");       /* discard saved address */
            depth--;
        }
        return;

    case ND_POST_DEC:
        sz = inc_dec_step(n->lhs);
        if (n->lhs->ty && type_is_fp128(n->lhs->ty)) {
            gen_addr(n->lhs);
            push();                        /* push address */
            emit("mov x1, x0");
            emit_load(n->lhs->ty);         /* q0 = old */
            push_q();                      /* save old (result) */
            emit("fmov d0, #1.0");
            emit_double_to_fp128();        /* q0 = 1.0L */
            emit("mov v1.16b, v0.16b");    /* q1 = 1.0L */
            emit("ldr q0, [sp]");          /* q0 = old */
            emit("bl __subtf3");           /* q0 = new */
            emit("ldr x1, [sp, #16]");     /* x1 = address */
            emit_store(n->lhs->ty);        /* store new */
            emit("ldr q0, [sp]");          /* restore old */
            emit("add sp, sp, #32");       /* discard old + address */
            depth -= 2;
        } else if (n->lhs->ty && type_is_flonum(n->lhs->ty)) {
            gen_addr(n->lhs);
            push();
            emit("mov x1, x0");
            emit_load(n->lhs->ty);
            emit("fmov d1, d0");
            emit("fmov d2, #1.0");
            emit("fsub d0, d0, d2");
            pop("x1");
            emit_store(n->lhs->ty);
            emit("fmov d0, d1");
        } else {
            gen_addr(n->lhs);
            push();                        /* push address */
            emit("mov x1, x0");
            if (n->lhs->kind == ND_MEMBER && n->lhs->bit_width > 0) {
                emit_bitfield_load(n->lhs);
            } else {
                emit_load(n->lhs->ty);
            }
            push();                        /* push original value */
            emit("sub x0, x0, #%d", sz);
            emit("ldr x1, [sp, #16]");    /* reload address */
            if (n->lhs->kind == ND_MEMBER && n->lhs->bit_width > 0) {
                emit_bitfield_store(n->lhs);
            } else {
                emit_store(n->lhs->ty);
            }
            pop("x0");                     /* restore original value */
            emit("add sp, sp, #16");       /* discard saved address */
            depth--;
        }
        return;

    /* &&label -- address of a label */
    case ND_LABEL_ADDR:
    {
        /* Use section_name if available (enclosing function name for
         * labels referenced from nested functions), else current_func */
        const char *lbl_fn;
        lbl_fn = n->section_name ? n->section_name : current_func;
        emit("adrp x0, .L.label.%s.%s", lbl_fn, n->name);
        emit("add x0, x0, :lo12:.L.label.%s.%s",
             lbl_fn, n->name);
        return;
    }

    /* __builtin_{add,sub,mul}_overflow */
    case ND_BUILTIN_OVERFLOW:
    {
        /* evaluate a -> push, evaluate b -> push,
         * evaluate result ptr -> push */
        gen_expr(n->lhs);   /* a */
        push();
        gen_expr(n->rhs);   /* b */
        push();
        gen_expr(n->body);  /* &result (pointer) */
        push();

        /* pop result ptr -> x3, b -> x2, a -> x1 */
        pop("x3");
        pop("x2");
        pop("x1");

        {
            /* determine result type signedness and size */
            int res_unsigned, res_32;
            struct type *rty;
            rty = NULL;
            if (n->body && n->body->ty && n->body->ty->base)
                rty = n->body->ty->base;
            res_unsigned = (rty && rty->is_unsigned);
            res_32 = (rty && rty->size <= 4);

            if (n->val == 0) {
                /* add overflow */
                if (res_32) {
                    emit("adds w0, w1, w2");
                    emit("cset x4, %s", res_unsigned ? "cs" : "vs");
                    emit("str w0, [x3]");
                } else {
                    emit("adds x0, x1, x2");
                    emit("cset x4, %s", res_unsigned ? "cs" : "vs");
                    emit("str x0, [x3]");
                }
                emit("mov x0, x4");
            } else if (n->val == 1) {
                /* sub overflow */
                if (res_32) {
                    emit("subs w0, w1, w2");
                    emit("cset x4, %s", res_unsigned ? "cc" : "vs");
                    emit("str w0, [x3]");
                } else {
                    emit("subs x0, x1, x2");
                    emit("cset x4, %s", res_unsigned ? "cc" : "vs");
                    emit("str x0, [x3]");
                }
                emit("mov x0, x4");
            } else {
                /* mul overflow */
                if (res_32 && res_unsigned) {
                    emit("umull x0, w1, w2");
                    emit("lsr x4, x0, #32");
                    emit("cmp x4, #0");
                    emit("cset x5, ne");
                    emit("str w0, [x3]");
                } else if (res_32) {
                    emit("smull x0, w1, w2");
                    emit("cmp x0, w0, sxtw");
                    emit("cset x5, ne");
                    emit("str w0, [x3]");
                } else if (res_unsigned) {
                    emit("umulh x4, x1, x2");
                    emit("mul x0, x1, x2");
                    emit("cmp x4, #0");
                    emit("cset x5, ne");
                    emit("str x0, [x3]");
                } else {
                    emit("smulh x4, x1, x2");
                    emit("mul x0, x1, x2");
                    emit("cmp x4, x0, asr #63");
                    emit("cset x5, ne");
                    emit("str x0, [x3]");
                }
                emit("mov x0, x5");
            }
        }
        return;
    }

    default:
        break;
    }

    /* floating-point unary negation: SUB(0, expr) -> fneg */
    if (n->kind == ND_SUB && n->ty != NULL && type_is_fp128(n->ty) &&
        n->lhs != NULL && n->lhs->kind == ND_NUM && n->lhs->val == 0) {
        gen_expr(n->rhs);
        emit_scalar_to_fp128(n->rhs != NULL ? n->rhs->ty : NULL);
        emit_fp128_negate();
        return;
    }

    /* complex arithmetic: component-wise operations.
     * Complex values in memory: address in x0.
     * Scalar values in d0 (float) or d0 (double) or x0 (int).
     * Result written to stack temp, address in x0. */
    if (n->ty != NULL &&
        (n->ty->kind == TY_COMPLEX_DOUBLE ||
         n->ty->kind == TY_COMPLEX_FLOAT) &&
        (n->kind == ND_ADD || n->kind == ND_SUB ||
         n->kind == ND_MUL || n->kind == ND_DIV)) {
        int is_cf;
        int rhs_cplx, lhs_cplx;
        int rhs_is_call;
        int lhs_is_call;
        is_cf = (n->ty->kind == TY_COMPLEX_FLOAT);
        rhs_cplx = (n->rhs && n->rhs->ty &&
                    (n->rhs->ty->kind == TY_COMPLEX_DOUBLE ||
                     n->rhs->ty->kind == TY_COMPLEX_FLOAT));
        lhs_cplx = (n->lhs && n->lhs->ty &&
                    (n->lhs->ty->kind == TY_COMPLEX_DOUBLE ||
                     n->lhs->ty->kind == TY_COMPLEX_FLOAT));
        rhs_is_call = (n->rhs && (n->rhs->kind == ND_CALL ||
                       n->rhs->kind == ND_STMT_EXPR));
        lhs_is_call = (n->lhs && (n->lhs->kind == ND_CALL ||
                       n->lhs->kind == ND_STMT_EXPR));
        /* eval rhs */
        gen_expr(n->rhs);
        /* load rhs components into d2/d3 (or s2/s3) */
        if (rhs_cplx) {
            if (rhs_is_call) {
                /* result in d0/d1 from call */
                if (is_cf) {
                    emit("fmov s2, s0");
                    emit("fmov s3, s1");
                } else {
                    emit("fmov d2, d0");
                    emit("fmov d3, d1");
                }
            } else {
                if (is_cf) {
                    emit("ldr s2, [x0]");
                    emit("ldr s3, [x0, #4]");
                } else {
                    emit("ldr d2, [x0]");
                    emit("ldr d3, [x0, #8]");
                }
            }
        } else {
            /* scalar: real = value, imag = 0 */
            if (n->rhs && n->rhs->ty && type_is_fp128(n->rhs->ty)) {
                if (is_cf) {
                    emit_fp128_to_float_in_d0();
                    emit("fmov s2, s0");
                    emit("movi v3.2s, #0");
                } else {
                    emit_fp128_to_double();
                    emit("fmov d2, d0");
                    emit("fmov d3, xzr");
                }
            } else if (n->rhs && n->rhs->ty && type_is_flonum(n->rhs->ty)) {
                if (is_cf) {
                    emit("fmov s2, s0");
                    emit("movi v3.2s, #0");
                } else {
                    emit("fmov d2, d0");
                    emit("fmov d3, xzr");
                }
            } else {
                if (is_cf) {
                    emit("scvtf s2, w0");
                    emit("movi v3.2s, #0");
                } else {
                    emit("scvtf d2, x0");
                    emit("fmov d3, xzr");
                }
            }
        }
        /* save rhs components on stack */
        emit("sub sp, sp, #16");
        depth++;
        if (is_cf) {
            emit("str s2, [sp]");
            emit("str s3, [sp, #4]");
        } else {
            emit("str d2, [sp]");
            emit("str d3, [sp, #8]");
        }
        /* eval lhs */
        gen_expr(n->lhs);
        /* load lhs components into d0/d1 (or s0/s1) */
        if (lhs_cplx) {
            if (lhs_is_call) {
                /* result in d0/d1 from call -- already there */
            } else {
                if (is_cf) {
                    emit("ldr s1, [x0, #4]");
                    emit("ldr s0, [x0]");
                } else {
                    emit("ldr d1, [x0, #8]");
                    emit("ldr d0, [x0]");
                }
            }
        } else {
            /* scalar: real = value, imag = 0 */
            if (n->lhs && n->lhs->ty && type_is_fp128(n->lhs->ty)) {
                if (is_cf) {
                    emit_fp128_to_float_in_d0();
                    emit("movi v1.2s, #0");
                } else {
                    emit_fp128_to_double();
                    emit("fmov d1, xzr");
                }
            } else if (n->lhs && n->lhs->ty && type_is_flonum(n->lhs->ty)) {
                /* d0 already has the value */
                if (is_cf) {
                    emit("movi v1.2s, #0");
                } else {
                    emit("fmov d1, xzr");
                }
            } else {
                if (is_cf) {
                    emit("scvtf s0, w0");
                    emit("movi v1.2s, #0");
                } else {
                    emit("scvtf d0, x0");
                    emit("fmov d1, xzr");
                }
            }
        }
        /* restore rhs components */
        if (is_cf) {
            emit("ldr s2, [sp]");
            emit("ldr s3, [sp, #4]");
        } else {
            emit("ldr d2, [sp]");
            emit("ldr d3, [sp, #8]");
        }
        emit("add sp, sp, #16");
        depth--;
        /* perform operation */
        if (n->kind == ND_MUL) {
            /* (a+bi)(c+di) = (ac-bd) + (ad+bc)i */
            if (is_cf) {
                emit("fmul s4, s0, s2"); /* ac */
                emit("fmul s5, s1, s3"); /* bd */
                emit("fsub s4, s4, s5"); /* ac-bd */
                emit("fmul s5, s0, s3"); /* ad */
                emit("fmul s6, s1, s2"); /* bc */
                emit("fadd s5, s5, s6"); /* ad+bc */
                emit("fmov s0, s4");
                emit("fmov s1, s5");
            } else {
                emit("fmul d4, d0, d2"); /* ac */
                emit("fmul d5, d1, d3"); /* bd */
                emit("fsub d4, d4, d5"); /* ac-bd */
                emit("fmul d5, d0, d3"); /* ad */
                emit("fmul d6, d1, d2"); /* bc */
                emit("fadd d5, d5, d6"); /* ad+bc */
                emit("fmov d0, d4");
                emit("fmov d1, d5");
            }
        } else if (n->kind == ND_DIV) {
            /* (a+bi)/(c+di) = ((ac+bd)/(c^2+d^2)) + ((bc-ad)/(c^2+d^2))i */
            if (is_cf) {
                emit("fmul s4, s2, s2"); /* c^2 */
                emit("fmul s5, s3, s3"); /* d^2 */
                emit("fadd s4, s4, s5"); /* c^2+d^2 */
                emit("fmul s5, s0, s2"); /* ac */
                emit("fmul s6, s1, s3"); /* bd */
                emit("fadd s5, s5, s6"); /* ac+bd */
                emit("fdiv s5, s5, s4"); /* (ac+bd)/(c^2+d^2) */
                emit("fmul s6, s1, s2"); /* bc */
                emit("fmul s7, s0, s3"); /* ad */
                emit("fsub s6, s6, s7"); /* bc-ad */
                emit("fdiv s6, s6, s4"); /* (bc-ad)/(c^2+d^2) */
                emit("fmov s0, s5");
                emit("fmov s1, s6");
            } else {
                emit("fmul d4, d2, d2"); /* c^2 */
                emit("fmul d5, d3, d3"); /* d^2 */
                emit("fadd d4, d4, d5"); /* c^2+d^2 */
                emit("fmul d5, d0, d2"); /* ac */
                emit("fmul d6, d1, d3"); /* bd */
                emit("fadd d5, d5, d6"); /* ac+bd */
                emit("fdiv d5, d5, d4"); /* (ac+bd)/(c^2+d^2) */
                emit("fmul d6, d1, d2"); /* bc */
                emit("fmul d7, d0, d3"); /* ad */
                emit("fsub d6, d6, d7"); /* bc-ad */
                emit("fdiv d6, d6, d4"); /* (bc-ad)/(c^2+d^2) */
                emit("fmov d0, d5");
                emit("fmov d1, d6");
            }
        } else if (n->kind == ND_ADD) {
            if (is_cf) {
                emit("fadd s0, s0, s2");
                emit("fadd s1, s1, s3");
            } else {
                emit("fadd d0, d0, d2");
                emit("fadd d1, d1, d3");
            }
        } else {
            if (is_cf) {
                emit("fsub s0, s0, s2");
                emit("fsub s1, s1, s3");
            } else {
                emit("fsub d0, d0, d2");
                emit("fsub d1, d1, d3");
            }
        }
        /* store result to stack temp, return address in x0 */
        emit("sub sp, sp, #16");
        depth++;
        if (is_cf) {
            emit("str s0, [sp]");
            emit("str s1, [sp, #4]");
        } else {
            emit("str d0, [sp]");
            emit("str d1, [sp, #8]");
        }
        emit("mov x0, sp");
        return;
    }

    /* complex comparison: a != b means either component differs.
     * Handle mixed scalar/complex: scalar becomes (val, 0). */
    if ((n->kind == ND_EQ || n->kind == ND_NE) &&
        ((n->lhs && n->lhs->ty &&
          (n->lhs->ty->kind == TY_COMPLEX_DOUBLE ||
           n->lhs->ty->kind == TY_COMPLEX_FLOAT)) ||
         (n->rhs && n->rhs->ty &&
          (n->rhs->ty->kind == TY_COMPLEX_DOUBLE ||
           n->rhs->ty->kind == TY_COMPLEX_FLOAT)))) {
        int lhs_cplx, rhs_cplx;
        lhs_cplx = (n->lhs && n->lhs->ty &&
                    (n->lhs->ty->kind == TY_COMPLEX_DOUBLE ||
                     n->lhs->ty->kind == TY_COMPLEX_FLOAT));
        rhs_cplx = (n->rhs && n->rhs->ty &&
                    (n->rhs->ty->kind == TY_COMPLEX_DOUBLE ||
                     n->rhs->ty->kind == TY_COMPLEX_FLOAT));
        /* eval rhs, get components into d2/d3 (as doubles) */
        gen_expr(n->rhs);
        if (rhs_cplx) {
            if (n->rhs->ty->kind == TY_COMPLEX_FLOAT) {
                emit("ldr s0, [x0]");
                emit("fcvt d2, s0");
                emit("ldr s0, [x0, #4]");
                emit("fcvt d3, s0");
            } else {
                emit("ldr d2, [x0]");
                emit("ldr d3, [x0, #8]");
            }
        } else {
            if (n->rhs->ty && type_is_flonum(n->rhs->ty))
                emit("fmov d2, d0");
            else
                emit("scvtf d2, x0");
            emit("fmov d3, xzr");
        }
        emit("fmov d0, d2");
        push_fp();
        emit("fmov d0, d3");
        push_fp();
        /* eval lhs, get components into d0/d1 */
        gen_expr(n->lhs);
        if (lhs_cplx) {
            if (n->lhs->ty->kind == TY_COMPLEX_FLOAT) {
                emit("ldr s0, [x0]");
                emit("fcvt d0, s0");
                emit("ldr s1, [x0, #4]");
                emit("fcvt d1, s1");
            } else {
                emit("ldr d0, [x0]");
                emit("ldr d1, [x0, #8]");
            }
        } else {
            if (n->lhs->ty && type_is_flonum(n->lhs->ty))
                ; /* d0 already has value */
            else
                emit("scvtf d0, x0");
            emit("fmov d1, xzr");
        }
        pop_fp("d3");
        pop_fp("d2");
        emit("fcmp d0, d2");
        emit("cset x0, ne");
        emit("fcmp d1, d3");
        emit("cset x1, ne");
        if (n->kind == ND_NE) {
            emit("orr x0, x0, x1");
        } else {
            emit("orr x0, x0, x1");
            emit("eor x0, x0, #1");
        }
        return;
    }

    /* long double (fp128) binary arithmetic and comparisons via libcalls */
    if ((n->kind == ND_ADD || n->kind == ND_SUB ||
         n->kind == ND_MUL || n->kind == ND_DIV) &&
        n->ty != NULL && type_is_fp128(n->ty)) {
        const char *fn;

        fn = NULL;
        if (n->kind == ND_ADD) fn = "__addtf3";
        else if (n->kind == ND_SUB) fn = "__subtf3";
        else if (n->kind == ND_MUL) fn = "__multf3";
        else if (n->kind == ND_DIV) fn = "__divtf3";
        if (fn == NULL) {
            goto after_fp128;
        }

        /* rhs -> q0 */
        gen_expr(n->rhs);
        emit_scalar_to_fp128(n->rhs != NULL ? n->rhs->ty : NULL);
        push_q();

        /* lhs -> q0 */
        gen_expr(n->lhs);
        emit_scalar_to_fp128(n->lhs != NULL ? n->lhs->ty : NULL);
        pop_q("q1");
        emit("bl %s", fn);
        return;
    }

    if ((n->kind == ND_EQ || n->kind == ND_NE ||
         n->kind == ND_LT || n->kind == ND_LE) &&
        ((n->lhs && n->lhs->ty && type_is_fp128(n->lhs->ty)) ||
         (n->rhs && n->rhs->ty && type_is_fp128(n->rhs->ty)))) {
        const char *fn;

        fn = NULL;
        if (n->kind == ND_EQ) fn = "__eqtf2";
        else if (n->kind == ND_NE) fn = "__netf2";
        else if (n->kind == ND_LT) fn = "__lttf2";
        else if (n->kind == ND_LE) fn = "__letf2";
        if (fn == NULL) {
            goto after_fp128;
        }

        /* rhs -> q0 */
        gen_expr(n->rhs);
        emit_scalar_to_fp128(n->rhs != NULL ? n->rhs->ty : NULL);
        push_q();

        /* lhs -> q0 */
        gen_expr(n->lhs);
        emit_scalar_to_fp128(n->lhs != NULL ? n->lhs->ty : NULL);
        pop_q("q1");
        emit("bl %s", fn);

        if (n->kind == ND_EQ) {
            emit("cmp w0, #0");
            emit("cset x0, eq");
        } else if (n->kind == ND_NE) {
            emit("cmp w0, #0");
            emit("cset x0, ne");
        } else if (n->kind == ND_LT) {
            emit("cmp w0, #0");
            emit("cset x0, lt");
        } else {
            emit("cmp w0, #0");
            emit("cset x0, le");
        }
        return;
    }
after_fp128:

    /* floating-point binary operations */
    if (n->ty != NULL && type_is_real_flonum(n->ty)) {
        /* eval rhs -> d0, push; eval lhs -> d0; pop rhs to d1 */
        int rhs_is_int, lhs_is_int;
        rhs_is_int = (n->rhs && n->rhs->ty &&
                      !type_is_real_flonum(n->rhs->ty));
        lhs_is_int = (n->lhs && n->lhs->ty &&
                      !type_is_real_flonum(n->lhs->ty));
        gen_expr(n->rhs);
        if (rhs_is_int) {
            if (n->rhs->ty && n->rhs->ty->is_unsigned &&
                n->rhs->ty->size <= 4)
                emit("ucvtf d0, w0");
            else if (n->rhs->ty && n->rhs->ty->size <= 4)
                emit("scvtf d0, w0");
            else if (n->rhs->ty && n->rhs->ty->is_unsigned)
                emit("ucvtf d0, x0");
            else
                emit("scvtf d0, x0");
        }
        push_fp();
        gen_expr(n->lhs);
        if (lhs_is_int) {
            if (n->lhs->ty && n->lhs->ty->is_unsigned &&
                n->lhs->ty->size <= 4)
                emit("ucvtf d0, w0");
            else if (n->lhs->ty && n->lhs->ty->size <= 4)
                emit("scvtf d0, w0");
            else if (n->lhs->ty && n->lhs->ty->is_unsigned)
                emit("ucvtf d0, x0");
            else
                emit("scvtf d0, x0");
        }
        pop_fp("d1");
        /* now d0 = lhs, d1 = rhs */
        switch (n->kind) {
        case ND_ADD:
            emit("fadd d0, d0, d1");
            break;
        case ND_SUB:
            emit("fsub d0, d0, d1");
            break;
        case ND_MUL:
            emit("fmul d0, d0, d1");
            break;
        case ND_DIV:
            emit("fdiv d0, d0, d1");
            break;
        default:
            goto not_fp_arith;
        }
        /* truncate to float precision if result type is float */
        if (n->ty != NULL && n->ty->kind == TY_FLOAT) {
            emit("fcvt s0, d0");
            emit("fcvt d0, s0");
        }
        return;
    not_fp_arith: ;
    }

    /* FP comparisons: result type is int, but operands are float */
    if ((n->kind == ND_EQ || n->kind == ND_NE ||
         n->kind == ND_LT || n->kind == ND_LE) &&
        ((n->lhs != NULL && n->lhs->ty != NULL &&
          type_is_real_flonum(n->lhs->ty)) ||
         (n->rhs != NULL && n->rhs->ty != NULL &&
          type_is_real_flonum(n->rhs->ty)))) {
        int rhs_is_int, lhs_is_int;
        int cmp_float;
        int lhs_is_double, rhs_is_double;
        rhs_is_int = (n->rhs && n->rhs->ty &&
                      !type_is_real_flonum(n->rhs->ty));
        lhs_is_int = (n->lhs && n->lhs->ty &&
                      !type_is_real_flonum(n->lhs->ty));
        /* Determine if comparison should be in float (s) or
         * double (d) precision per C usual arithmetic conversions:
         * if either operand is double, compare in double;
         * otherwise compare in float. */
        lhs_is_double = (n->lhs && n->lhs->ty &&
                         n->lhs->ty->kind == TY_DOUBLE);
        rhs_is_double = (n->rhs && n->rhs->ty &&
                         n->rhs->ty->kind == TY_DOUBLE);
        cmp_float = !lhs_is_double && !rhs_is_double;
        gen_expr(n->rhs);
        if (rhs_is_int) {
            const char *cvt_r;
            cvt_r = (n->rhs->ty && n->rhs->ty->size <= 4) ? "w0" : "x0";
            if (cmp_float) {
                /* convert int to float precision for comparison */
                if (n->rhs->ty && n->rhs->ty->is_unsigned)
                    emit("ucvtf s0, %s", cvt_r);
                else
                    emit("scvtf s0, %s", cvt_r);
                emit("fcvt d0, s0");
            } else {
                if (n->rhs->ty && n->rhs->ty->is_unsigned)
                    emit("ucvtf d0, %s", cvt_r);
                else
                    emit("scvtf d0, %s", cvt_r);
            }
        } else if (cmp_float) {
            /* float operand: ensure float precision rounding */
            emit("fcvt s0, d0");
            emit("fcvt d0, s0");
        }
        push_fp();
        gen_expr(n->lhs);
        if (lhs_is_int) {
            const char *cvt_r;
            cvt_r = (n->lhs->ty && n->lhs->ty->size <= 4) ? "w0" : "x0";
            if (cmp_float) {
                if (n->lhs->ty && n->lhs->ty->is_unsigned)
                    emit("ucvtf s0, %s", cvt_r);
                else
                    emit("scvtf s0, %s", cvt_r);
                emit("fcvt d0, s0");
            } else {
                if (n->lhs->ty && n->lhs->ty->is_unsigned)
                    emit("ucvtf d0, %s", cvt_r);
                else
                    emit("scvtf d0, %s", cvt_r);
            }
        } else if (cmp_float) {
            emit("fcvt s0, d0");
            emit("fcvt d0, s0");
        }
        pop_fp("d1");
        emit("fcmp d0, d1");
        switch (n->kind) {
        case ND_EQ:
            emit("cset x0, eq");
            return;
        case ND_NE:
            emit("cset x0, ne");
            return;
        case ND_LT:
            emit("cset x0, mi");
            return;
        case ND_LE:
            emit("cset x0, ls");
            return;
        default:
            break;
        }
    }

    /* binary operations: eval rhs, push, eval lhs, pop rhs to x1 */
    gen_expr(n->rhs);
    push();
    gen_expr(n->lhs);
    pop("x1");

    /* select w-register (32-bit) or x-register (64-bit) for arithmetic */
    {
    int w32;
    const char *r0, *r1, *r2;
    const char *cr0, *cr1; /* comparison register names (from operand type) */

    w32 = is_32bit(n->ty);
    r0 = w32 ? "w0" : "x0";
    r1 = w32 ? "w1" : "x1";
    r2 = w32 ? "w2" : "x2";

    /* for comparisons, use the wider of the two operand types */
    {
        int cmp32;
        int lhs32, rhs32;
        lhs32 = (n->lhs && is_32bit(n->lhs->ty));
        rhs32 = (n->rhs && is_32bit(n->rhs->ty));
        cmp32 = lhs32 && rhs32;
        cr0 = cmp32 ? "w0" : "x0";
        cr1 = cmp32 ? "w1" : "x1";
        /* extend 32-bit operands when comparing with 64-bit */
        if (!cmp32 && lhs32) {
            if (n->lhs->ty && n->lhs->ty->is_unsigned) {
                emit("mov w0, w0"); /* zero-extend */
            } else {
                emit("sxtw x0, w0");
            }
        }
        if (!cmp32 && rhs32) {
            if (n->rhs->ty && n->rhs->ty->is_unsigned) {
                emit("mov w1, w1"); /* zero-extend */
            } else {
                emit("sxtw x1, w1");
            }
        }
    }

    /* now x0 = lhs, x1 = rhs */
    switch (n->kind) {
    case ND_ADD:
        /* pointer arithmetic: scale the integer operand */
        if (n->ty != NULL &&
            (n->ty->kind == TY_PTR || n->ty->kind == TY_ARRAY) &&
            n->ty->base != NULL) {
            int lhs_is_ptr;
            lhs_is_ptr = (n->lhs && n->lhs->ty &&
                          (n->lhs->ty->kind == TY_PTR ||
                           n->lhs->ty->kind == TY_ARRAY));
            sz = n->ty->base->size;
            if (sz > 1) {
                emit("mov x2, #%d", sz);
                /* scale the integer operand, not the pointer */
                if (lhs_is_ptr) {
                    emit("mul x1, x1, x2");  /* rhs is int */
                } else {
                    emit("mul x0, x0, x2");  /* lhs is int */
                }
            }
        }
        emit("add %s, %s, %s", r0, r0, r1);
        emit_bitfield_result_mask(n);
        return;

    case ND_SUB:
        if (n->lhs != NULL && n->lhs->ty != NULL &&
            (n->lhs->ty->kind == TY_PTR || n->lhs->ty->kind == TY_ARRAY) &&
            n->lhs->ty->base != NULL &&
            n->rhs != NULL && n->rhs->ty != NULL &&
            (n->rhs->ty->kind == TY_PTR || n->rhs->ty->kind == TY_ARRAY)) {
            /* pointer - pointer: divide by element size */
            emit("sub x0, x0, x1");
            sz = n->lhs->ty->base->size;
            if (sz > 1) {
                emit("mov x2, #%d", sz);
                emit("sdiv x0, x0, x2");
            }
        } else if (n->ty != NULL &&
                   (n->ty->kind == TY_PTR || n->ty->kind == TY_ARRAY) &&
                   n->ty->base != NULL) {
            /* pointer - int: scale the integer */
            sz = n->ty->base->size;
            if (sz > 1) {
                emit("mov x2, #%d", sz);
                emit("mul x1, x1, x2");
            }
            emit("sub x0, x0, x1");
        } else {
            emit("sub %s, %s, %s", r0, r0, r1);
        }
        emit_bitfield_result_mask(n);
        return;

    case ND_MUL:
        emit("mul %s, %s, %s", r0, r0, r1);
        emit_bitfield_result_mask(n);
        return;

    case ND_DIV:
        if (n->ty != NULL && n->ty->is_unsigned) {
            emit("udiv %s, %s, %s", r0, r0, r1);
        } else {
            emit("sdiv %s, %s, %s", r0, r0, r1);
        }
        emit_bitfield_result_mask(n);
        return;

    case ND_MOD:
        if (n->ty != NULL && n->ty->is_unsigned) {
            emit("udiv %s, %s, %s", r2, r0, r1);
        } else {
            emit("sdiv %s, %s, %s", r2, r0, r1);
        }
        emit("msub %s, %s, %s, %s", r0, r2, r1, r0);
        emit_bitfield_result_mask(n);
        return;

    case ND_BITAND:
        emit("and %s, %s, %s", r0, r0, r1);
        emit_bitfield_result_mask(n);
        return;

    case ND_BITOR:
        emit("orr %s, %s, %s", r0, r0, r1);
        emit_bitfield_result_mask(n);
        return;

    case ND_BITXOR:
        emit("eor %s, %s, %s", r0, r0, r1);
        emit_bitfield_result_mask(n);
        return;

    case ND_SHL:
        emit("lsl %s, %s, %s", r0, r0, r1);
        emit_bitfield_result_mask(n);
        return;

    case ND_SHR:
        if (n->ty != NULL && n->ty->is_unsigned) {
            emit("lsr %s, %s, %s", r0, r0, r1);
        } else {
            emit("asr %s, %s, %s", r0, r0, r1);
        }
        emit_bitfield_result_mask(n);
        return;

    case ND_EQ:
        emit("cmp %s, %s", cr0, cr1);
        emit("cset w0, eq");
        return;

    case ND_NE:
        emit("cmp %s, %s", cr0, cr1);
        emit("cset w0, ne");
        return;

    case ND_LT:
        emit("cmp %s, %s", cr0, cr1);
        {
            /* use common type to decide signed vs unsigned */
            struct type *cmp_ty;
            cmp_ty = type_common(
                n->lhs ? n->lhs->ty : NULL,
                n->rhs ? n->rhs->ty : NULL);
            if (cmp_ty != NULL && cmp_ty->is_unsigned) {
                emit("cset w0, lo");
            } else {
                emit("cset w0, lt");
            }
        }
        return;

    case ND_LE:
        emit("cmp %s, %s", cr0, cr1);
        {
            struct type *cmp_ty;
            cmp_ty = type_common(
                n->lhs ? n->lhs->ty : NULL,
                n->rhs ? n->rhs->ty : NULL);
            if (cmp_ty != NULL && cmp_ty->is_unsigned) {
                emit("cset w0, ls");
            } else {
                emit("cset w0, le");
            }
        }
        return;

    /* Labels and gotos can appear inside statement expressions.
     * Handle them here so the last labeled expression can produce a value. */
    case ND_LABEL:
        emit_label(".L.label.%s.%s", current_func, n->name);
        if (n->body != NULL) {
            gen_expr(n->body);
        }
        return;

    case ND_GOTO:
    {
        const char *goto_fn2;
        goto_fn2 = n->section_name ? n->section_name : current_func;
        emit("b .L.label.%s.%s", goto_fn2, n->name);
        return;
    }

    case ND_BLOCK:
        /* statement expression ({ ... }) used as expression.
         * Generate all body statements; the last expression's
         * result remains in x0. */
        {
            struct node *cur2;
            struct node *last2;
            last2 = NULL;
            for (cur2 = n->body; cur2 != NULL; cur2 = cur2->next) {
                last2 = cur2;
            }
            for (cur2 = n->body; cur2 != NULL; cur2 = cur2->next) {
                if (cur2->next == NULL && last2 != NULL
                    && last2->kind != ND_RETURN
                    && last2->kind != ND_IF
                    && last2->kind != ND_WHILE
                    && last2->kind != ND_FOR
                    && last2->kind != ND_DO
                    && last2->kind != ND_BLOCK
                    && last2->kind != ND_SWITCH
                    && last2->kind != ND_GCC_ASM) {
                    gen_expr(cur2);
                } else {
                    gen_stmt(cur2);
                }
            }
        }
        return;

    default:
        fprintf(stderr, "gen_expr: unknown node kind %d\n", n->kind);
        exit(1);
    }
    } /* end w-register block */
}

/* ---- statement generation ---- */

/*
 * gen_stmt - generate code for a statement node.
 */
static void gen_stmt(struct node *n)
{
    int lbl;
    int saved_break;
    int saved_continue;
    struct node *cur;

    if (n == NULL) {
        return;
    }

    switch (n->kind) {
    case ND_RETURN:
        if (n->lhs != NULL) {
            int ret_is_call;
            ret_is_call = (n->lhs->kind == ND_CALL ||
                           n->lhs->kind == ND_STMT_EXPR);
            gen_expr(n->lhs);
            /* If returning a struct/union by value, gen_expr left
             * the address of the struct in x0 (for lvalue expressions)
             * or the struct value in x0/x1 (for ND_CALL / ND_STMT_EXPR).
             * AAPCS64 requires small structs to be returned in
             * x0/x1 by value. */
            /* Complex return: load components into FP regs.
             * _Complex double -> d0 (real), d1 (imag)
             * _Complex float  -> s0 (real), s1 (imag)
             * gen_expr left address of complex local in x0. */
            if (current_func_ret &&
                current_func_ret->kind == TY_COMPLEX_DOUBLE &&
                !ret_is_call) {
                emit("ldr d0, [x0]");
                emit("ldr d1, [x0, #8]");
            } else if (current_func_ret &&
                       current_func_ret->kind == TY_COMPLEX_FLOAT &&
                       !ret_is_call) {
                emit("ldr s0, [x0]");
                emit("ldr s1, [x0, #4]");
            } else if (current_func_ret &&
                (current_func_ret->kind == TY_STRUCT ||
                 current_func_ret->kind == TY_UNION) &&
                !ret_is_call) {
                int retsz;
                int hfa_k;
                retsz = current_func_ret->size;
                hfa_k = is_hfa(current_func_ret);
                if (hfa_k) {
                    /* HFA: return in FP regs d0-d3 or s0-s3 */
                    int hcnt, mi, msz;
                    hcnt = hfa_member_count(current_func_ret);
                    msz = hfa_k;
                    for (mi = 0; mi < hcnt; mi++) {
                        if (hfa_k == 4) {
                            emit("ldr s%d, [x0, #%d]",
                                 mi, mi * msz);
                        } else if (hfa_k == 16) {
                            emit("ldr q%d, [x0, #%d]",
                                 mi, mi * msz);
                        } else {
                            emit("ldr d%d, [x0, #%d]",
                                 mi, mi * msz);
                        }
                    }
                } else if (retsz <= 8) {
                    /* load struct value into x0 */
                    if (retsz <= 1) {
                        emit("ldrb w0, [x0]");
                    } else if (retsz <= 2) {
                        emit("ldrh w0, [x0]");
                    } else if (retsz <= 4) {
                        emit("ldr w0, [x0]");
                    } else {
                        emit("ldr x0, [x0]");
                    }
                } else if (retsz <= 16) {
                    /* load into x0 and x1 */
                    emit("ldr x1, [x0, #8]");
                    emit("ldr x0, [x0]");
                } else {
                    /* >16 bytes: copy to hidden return buffer
                     * (x8 was saved at [fp, #-ret_buf_offset]) */
                    int ci2;
                    emit("mov x2, x0");  /* source address */
                    if (ret_buf_offset <= 255) {
                        emit("ldr x1, [x29, #-%d]", ret_buf_offset);
                    } else {
                        emit_sub_fp("x1", ret_buf_offset);
                        emit("ldr x1, [x1]");
                    }
                    for (ci2 = 0; ci2 + 8 <= retsz; ci2 += 8) {
                        emit("ldr x3, [x2, #%d]", ci2);
                        emit("str x3, [x1, #%d]", ci2);
                    }
                    for (; ci2 + 4 <= retsz; ci2 += 4) {
                        emit("ldr w3, [x2, #%d]", ci2);
                        emit("str w3, [x1, #%d]", ci2);
                    }
                    for (; ci2 < retsz; ci2++) {
                        emit("ldrb w3, [x2, #%d]", ci2);
                        emit("strb w3, [x1, #%d]", ci2);
                    }
                    emit("mov x0, x1"); /* return buffer addr */
                }
            }
        }
        emit("b .L.return.%s", current_func);
        return;

    case ND_BLOCK:
        for (cur = n->body; cur != NULL; cur = cur->next) {
            gen_stmt(cur);
        }
        return;

    case ND_IF:
        lbl = new_label();
        gen_expr(n->cond);
        emit_truth_test(n->cond);
        emit("cmp x0, #0");
        if (n->els != NULL) {
            emit("b.eq .L.else.%d", lbl);
            gen_stmt(n->then_);
            emit("b .L.end.%d", lbl);
            emit_label(".L.else.%d", lbl);
            gen_stmt(n->els);
            emit_label(".L.end.%d", lbl);
        } else {
            emit("b.eq .L.end.%d", lbl);
            gen_stmt(n->then_);
            emit_label(".L.end.%d", lbl);
        }
        return;

    case ND_WHILE:
        lbl = new_label();
        saved_break = current_break_label;
        saved_continue = current_continue_label;
        current_break_label = lbl;
        current_continue_label = lbl;

        emit_label(".L.begin.%d", lbl);
        gen_expr(n->cond);
        emit_truth_test(n->cond);
        emit("cmp x0, #0");
        emit("b.eq .L.break.%d", lbl);
        gen_stmt(n->then_);
        emit_label(".L.continue.%d", lbl);
        emit("b .L.begin.%d", lbl);
        emit_label(".L.break.%d", lbl);

        current_break_label = saved_break;
        current_continue_label = saved_continue;
        return;

    case ND_FOR:
        lbl = new_label();
        saved_break = current_break_label;
        saved_continue = current_continue_label;
        current_break_label = lbl;
        current_continue_label = lbl;

        if (n->init != NULL) {
            gen_stmt(n->init);
        }
        emit_label(".L.begin.%d", lbl);
        if (n->cond != NULL) {
            gen_expr(n->cond);
            emit_truth_test(n->cond);
            emit("cmp x0, #0");
            emit("b.eq .L.break.%d", lbl);
        }
        gen_stmt(n->then_);
        emit_label(".L.continue.%d", lbl);
        if (n->inc != NULL) {
            gen_expr(n->inc);
        }
        emit("b .L.begin.%d", lbl);
        emit_label(".L.break.%d", lbl);

        current_break_label = saved_break;
        current_continue_label = saved_continue;
        return;

    case ND_DO:
        lbl = new_label();
        saved_break = current_break_label;
        saved_continue = current_continue_label;
        current_break_label = lbl;
        current_continue_label = lbl;

        emit_label(".L.begin.%d", lbl);
        gen_stmt(n->then_);
        emit_label(".L.continue.%d", lbl);
        gen_expr(n->cond);
        emit_truth_test(n->cond);
        emit("cmp x0, #0");
        emit("b.ne .L.begin.%d", lbl);
        emit_label(".L.break.%d", lbl);

        current_break_label = saved_break;
        current_continue_label = saved_continue;
        return;

    case ND_SWITCH:
    {
        int default_label;
        int sw32;
        const char *sw_val_reg;
        const char *sw_tmp_reg;

        lbl = new_label();
        saved_break = current_break_label;
        current_break_label = lbl;
        default_label = -1;
        /* use 32-bit regs for <=32-bit switch types so that
         * negative case values match correctly */
        sw32 = (n->cond->ty && n->cond->ty->size <= 4);
        sw_val_reg = sw32 ? "w9" : "x9";
        sw_tmp_reg = sw32 ? "w10" : "x10";

        gen_expr(n->cond);
        if (sw32) {
            emit("mov w0, w0"); /* zero-extend to 32-bit */
        }
        emit("mov x9, x0"); /* save switch value in callee-scratch */

        /*
         * Generate comparison jumps for each case.
         * Cases may be nested: each ND_CASE node's body
         * can itself be an ND_CASE (fall-through).  Walk
         * both the next chain and recurse into ND_CASE bodies
         * and ND_BLOCK children to find all ND_CASE nodes.
         */
        {
            struct node *stk[256];
            int sp;
            struct node *walk;

            sp = 0;
            walk = n->body;
            if (walk != NULL && walk->kind == ND_BLOCK) {
                walk = walk->body;
            }
            if (walk != NULL) {
                stk[sp++] = walk;
            }
            while (sp > 0) {
                walk = stk[--sp];
                while (walk != NULL) {
                    if (walk->kind == ND_CASE) {
                        long cv;
                        long cev;
                        walk->label_id = new_label();
                        /* truncate case values to switch width */
                        cv = walk->val;
                        cev = walk->case_end;
                        if (sw32) {
                            cv = (long)(unsigned int)cv;
                            cev = (long)(unsigned int)cev;
                        }
                        if (walk->is_default) {
                            default_label = walk->label_id;
                        } else if (walk->is_case_range) {
                            int skip_lbl;
                            skip_lbl = new_label();
                            if (cv >= 0 && cv <= 4095) {
                                emit("cmp %s, #%ld",
                                     sw_val_reg, cv);
                            } else {
                                emit_load_imm_reg(sw_tmp_reg,
                                                  cv);
                                emit("cmp %s, %s",
                                     sw_val_reg, sw_tmp_reg);
                            }
                            /* use unsigned compares for unsigned
                             * switch types */
                            if (n->cond->ty &&
                                n->cond->ty->is_unsigned) {
                                emit("b.lo .L.skip.%d",
                                     skip_lbl);
                            } else {
                                emit("b.lt .L.skip.%d",
                                     skip_lbl);
                            }
                            if (cev >= 0 && cev <= 4095) {
                                emit("cmp %s, #%ld",
                                     sw_val_reg, cev);
                            } else {
                                emit_load_imm_reg(sw_tmp_reg,
                                                  cev);
                                emit("cmp %s, %s",
                                     sw_val_reg, sw_tmp_reg);
                            }
                            if (n->cond->ty &&
                                n->cond->ty->is_unsigned) {
                                emit("b.ls .L.case.%d",
                                     walk->label_id);
                            } else {
                                emit("b.le .L.case.%d",
                                     walk->label_id);
                            }
                            emit_label(".L.skip.%d", skip_lbl);
                        } else {
                            if (cv >= 0 && cv <= 4095) {
                                emit("cmp %s, #%ld",
                                     sw_val_reg, cv);
                            } else {
                                emit_load_imm_reg(sw_tmp_reg,
                                                  cv);
                                emit("cmp %s, %s",
                                     sw_val_reg, sw_tmp_reg);
                            }
                            emit("b.eq .L.case.%d",
                                 walk->label_id);
                        }
                        /* recurse into case body for
                         * consecutive case labels */
                        if (walk->body != NULL && sp < 256) {
                            stk[sp++] = walk->body;
                        }
                    } else if (walk->kind == ND_BLOCK) {
                        /* enter block children */
                        if (walk->body != NULL && sp < 256) {
                            stk[sp++] = walk->body;
                        }
                    } else if (walk->kind != ND_SWITCH) {
                        /* case labels can appear inside
                         * any compound statement: loops
                         * (Duff's device), if, etc.
                         * But skip nested switches — their
                         * cases belong to the inner switch. */
                        if (walk->body != NULL && sp < 256) {
                            stk[sp++] = walk->body;
                        }
                        if (walk->then_ != NULL && sp < 256) {
                            stk[sp++] = walk->then_;
                        }
                        if (walk->els != NULL && sp < 256) {
                            stk[sp++] = walk->els;
                        }
                    }
                    walk = walk->next;
                }
            }
        }

        /* no case matched: jump to default if present, else break */
        if (default_label >= 0) {
            emit("b .L.case.%d", default_label);
        } else {
            emit("b .L.break.%d", lbl);
        }

        /* emit the case bodies (gen_stmt handles ND_BLOCK recursion) */
        gen_stmt(n->body);

        emit_label(".L.break.%d", lbl);
        current_break_label = saved_break;
        return;
    }

    case ND_CASE:
        emit_label(".L.case.%d", n->label_id);
        if (n->body != NULL) {
            gen_stmt(n->body);
        } else if (n->lhs != NULL) {
            gen_stmt(n->lhs);
        }
        return;

    case ND_BREAK:
        if (current_break_label < 0) {
            fprintf(stderr, "gen: break outside loop/switch\n");
            exit(1);
        }
        emit("b .L.break.%d", current_break_label);
        return;

    case ND_CONTINUE:
        if (current_continue_label < 0) {
            fprintf(stderr, "gen: continue outside loop\n");
            exit(1);
        }
        emit("b .L.continue.%d", current_continue_label);
        return;

    case ND_GOTO:
    {
        const char *goto_fn;
        goto_fn = n->section_name ? n->section_name : current_func;
        emit("b .L.label.%s.%s", goto_fn, n->name);
        return;
    }

    case ND_GOTO_INDIRECT:
        /* computed goto: goto *expr */
        gen_expr(n->lhs);
        emit("br x0");
        return;

    case ND_LABEL:
        emit_label(".L.label.%s.%s", current_func, n->name);
        gen_stmt(n->body);
        return;

    case ND_GCC_ASM:
        if (n->asm_data != NULL) {
            asm_emit(out, n->asm_data, current_func);
        }
        return;

    default:
        /* expression statement */
        gen_expr(n);
        return;
    }
}

/* ---- string literal collection ---- */

/*
 * emit_string_directive - emit string bytes plus a width-aware NUL.
 * Raw bytes in the string (newline, tab, backslash, quote, etc.) are
 * emitted as .byte values so embedded NULs are preserved.
 */
static void emit_string_directive(const char *s, int len, int width)
{
    int i;

    if (width <= 0) {
        width = 1;
    }

    if (s != NULL && len > 0) {
        for (i = 0; i < len; i++) {
            fprintf(out, "\t.byte %d\n",
                    (unsigned char)s[i]);
        }
    }
    while (width > 0) {
        fprintf(out, "\t.byte 0\n");
        width--;
    }
}

/*
 * collect_strings_node - recursively walk a single AST node (not its
 * next chain) and emit any ND_STR nodes into .rodata.
 *
 * Because compound assignments (a |= b => a = a | b) share the LHS
 * subtree, the same ND_STR node can be reachable from multiple paths.
 * We track visited nodes to avoid emitting duplicate labels.
 */
#define MAX_VISITED_STRINGS 4096
static struct node *visited_strings[MAX_VISITED_STRINGS];
static int n_visited_strings;

static int string_already_visited(struct node *n)
{
    int i;
    for (i = 0; i < n_visited_strings; i++) {
        if (visited_strings[i] == n) {
            return 1;
        }
    }
    if (n_visited_strings < MAX_VISITED_STRINGS) {
        visited_strings[n_visited_strings++] = n;
    }
    return 0;
}

static void collect_strings_node(struct node *n)
{
    struct node *cur;

    if (n == NULL) {
        return;
    }

    if (n->kind == ND_STR) {
        if (!string_already_visited(n)) {
            fprintf(out, ".LS%d:\n", n->label_id);
            emit_string_directive(n->name, (int)n->val,
                                 string_literal_elem_size(n));
        }
        return; /* strings have no children */
    }

    /* Don't recurse into init lists - their strings are collected
     * when walking through global var init fields */
    if (n->kind == ND_INIT_LIST) {
        for (cur = n->body; cur != NULL; cur = cur->next) {
            collect_strings_node(cur);
        }
        return;
    }

    collect_strings_node(n->lhs);
    collect_strings_node(n->rhs);
    /* walk the body chain (compound stmts and init list elements) */
    for (cur = n->body; cur != NULL; cur = cur->next) {
        collect_strings_node(cur);
    }
    collect_strings_node(n->cond);
    collect_strings_node(n->then_);
    collect_strings_node(n->els);
    collect_strings_node(n->init);
    collect_strings_node(n->inc);
    /* walk the args chain (function call arguments) */
    for (cur = n->args; cur != NULL; cur = cur->next) {
        collect_strings_node(cur);
    }
}

/*
 * collect_all_strings - walk every top-level node and collect strings.
 * The top-level next chain is iterated here; collect_strings_node
 * handles the recursive walk within each node.
 */
static void collect_all_strings(struct node *prog)
{
    struct node *n;

    n_visited_strings = 0;
    for (n = prog; n != NULL; n = n->next) {
        collect_strings_node(n);
    }
}

/* ---- FP literal collection ---- */

/*
 * Track visited FP literal nodes to avoid emitting duplicate labels.
 * Same approach as string deduplication.
 */
#define MAX_VISITED_FP 4096
static struct node *visited_fp[MAX_VISITED_FP];
static int n_visited_fp;

static int fp_already_visited(struct node *n)
{
    int i;
    for (i = 0; i < n_visited_fp; i++) {
        if (visited_fp[i] == n ||
            visited_fp[i]->label_id == n->label_id) {
            return 1;
        }
    }
    if (n_visited_fp < MAX_VISITED_FP) {
        visited_fp[n_visited_fp++] = n;
    }
    return 0;
}

/*
 * collect_fp_node - recursively walk an AST node and emit ND_FNUM literals.
 */
static void collect_fp_node(struct node *n)
{
    struct node *cur;

    if (n == NULL) {
        return;
    }

    if (n->kind == ND_FNUM && !fp_already_visited(n)) {
        union { double d; unsigned long u; } fpun;
        long double ld;
        unsigned long lo, hi;

        if (n->ty != NULL && type_is_fp128(n->ty)) {
            /* long double literal: widen the parsed double value */
            ld = (long double)n->fval;
            memcpy(&lo, &ld, 8);
            memcpy(&hi, (char *)&ld + 8, 8);
            fprintf(out, "\t.p2align 4\n");
            fprintf(out, ".LF%d:\n", n->label_id);
            fprintf(out, "\t.quad 0x%lx\n", lo);
            fprintf(out, "\t.quad 0x%lx\n", hi);
        } else {
            fpun.d = n->fval;
            fprintf(out, "\t.p2align 3\n");
            fprintf(out, ".LF%d:\n", n->label_id);
            fprintf(out, "\t.quad 0x%lx\n", fpun.u);
        }
    }

    collect_fp_node(n->lhs);
    collect_fp_node(n->rhs);
    /* walk the body chain (compound statement lists) */
    for (cur = n->body; cur != NULL; cur = cur->next) {
        collect_fp_node(cur);
    }
    collect_fp_node(n->cond);
    collect_fp_node(n->then_);
    collect_fp_node(n->els);
    collect_fp_node(n->init);
    collect_fp_node(n->inc);
    /* walk the args chain (function call arguments) */
    for (cur = n->args; cur != NULL; cur = cur->next) {
        collect_fp_node(cur);
    }
}

static void collect_all_fp(struct node *prog)
{
    struct node *n;

    n_visited_fp = 0;
    for (n = prog; n != NULL; n = n->next) {
        /* only walk function definitions; FP literals in global
         * variable initializers are emitted directly as .quad/.word
         * in the data section, not via .LF labels */
        if (n->kind == ND_FUNCDEF) {
            collect_fp_node(n);
        }
    }
}

/* ---- function definition ---- */

/*
 * gen_funcdef - generate code for a function definition.
 */
static void gen_funcdef(struct node *n)
{
    int stack_size;
    struct node *param;
    int needs_x8_save;

    current_func = n->name;
    current_func_ret = (n->ty && n->ty->kind == TY_FUNC)
                       ? n->ty->ret : NULL;
    current_break_label = -1;
    current_continue_label = -1;
    depth = 0;

    /* emit function directives */
    fprintf(out, "\n");
    if (n->section_name) {
        fprintf(out, "\t.section %s,\"ax\",%%progbits\n",
                n->section_name);
    } else if (cc_function_sections) {
        fprintf(out, "\t.section .text.%s,\"ax\",%%progbits\n",
                n->name);
    }
    if (n->attr_flags & GEN_ATTR_WEAK) {
        fprintf(out, "\t.weak %s\n", n->name);
    } else if (n->is_static) {
        fprintf(out, "\t.local %s\n", n->name);
    } else {
        fprintf(out, "\t.global %s\n", n->name);
    }
    fprintf(out, "\t.type %s, %%function\n", n->name);
    fprintf(out, "\t.p2align 2\n");
    emit_label("%s", n->name);

    /*
     * Calculate frame size.
     * The parser sets n->offset to the total local variable space.
     * Align locals to 16 bytes for ABI compliance.
     */
    /* Check if function returns a large struct (> 16 bytes, non-HFA).
     * If so, caller passes the return buffer address in x8.
     * We need to save x8 in the frame. */
    needs_x8_save = 0;
    ret_buf_offset = 0;
    if (current_func_ret &&
        (current_func_ret->kind == TY_STRUCT ||
         current_func_ret->kind == TY_UNION) &&
        current_func_ret->size > 16 &&
        !is_hfa(current_func_ret)) {
        needs_x8_save = 1;
    }

    /* Pre-allocate frame space for compound literals and init lists
     * so they don't use dynamic sp adjustments that break push/pop. */
    {
        int cl_offset;
        cl_offset = n->offset;
        /* reserve space for x8 save if needed */
        if (needs_x8_save) {
            cl_offset = align_to(cl_offset, 8) + 8;
        }
        assign_compound_lit_offsets(n->body, &cl_offset);
        stack_size = align_to(cl_offset, 16);
    }
    current_stack_size = stack_size;

    /* Detect leaf functions: no calls, not variadic, no parameters,
     * and no dynamic stack allocations (compound literals, init lists).
     * Functions with parameters use x29-relative addressing to save
     * args, so they need the full prologue that sets up x29.
     * Functions with dynamic stack allocs need x29 to restore sp. */
    current_is_leaf = 0;
    if (!node_has_call(n->body) &&
        !(n->ty && n->ty->is_variadic) &&
        n->args == NULL &&
        !node_has_dynamic_stack(n->body) &&
        !needs_x8_save) {
        current_is_leaf = 1;
    }

    if (current_is_leaf) {
        /* leaf prologue: just allocate stack, no fp/lr save */
        if (stack_size > 0) {
            emit_comment("leaf prologue");
            if (stack_size <= 4095) {
                emit("sub sp, sp, #%d", stack_size);
            } else {
                emit_load_imm_reg("x16", stack_size);
                emit("sub sp, sp, x16");
            }
        }
    } else {
        /* prologue: save fp/lr, set fp, then allocate locals below fp */
        emit_comment("prologue");
        emit("stp x29, x30, [sp, #-16]!");
        emit("mov x29, sp");
        if (stack_size > 0) {
            if (stack_size <= 4095) {
                emit("sub sp, sp, #%d", stack_size);
            } else if (stack_size <= 65535) {
                emit("mov x16, #%d", stack_size);
                emit("sub sp, sp, x16");
            } else {
                emit("movz x16, #%d", stack_size & 0xffff);
                emit("movk x16, #%d, lsl #16",
                     (stack_size >> 16) & 0xffff);
                emit("sub sp, sp, x16");
            }
        }
    }

    /* save x8 (hidden return buffer pointer) for large struct return */
    if (needs_x8_save) {
        /* x8 save goes at end of local variable area.
         * We reserved space for it above; compute its offset. */
        ret_buf_offset = align_to(n->offset, 8) + 8;
        emit_comment("save x8 (return buffer) to [fp, #-%d]",
                     ret_buf_offset);
        if (ret_buf_offset <= 255) {
            emit("str x8, [x29, #-%d]", ret_buf_offset);
        } else {
            emit_sub_fp("x10", ret_buf_offset);
            emit("str x8, [x10]");
        }
    }

    /* record variadic metadata for va_start / va_arg codegen */
    va_save_offset = n->va_save_offset;
    va_named_gp = n->va_named_gp;
    va_named_fp = n->va_named_fp;
    va_fp_save_offset = n->va_fp_save_offset;
    va_stack_start = n->va_stack_start;

    /*
     * For variadic functions, spill all 8 GP registers (x0-x7) into
     * the register save area so that va_arg can retrieve them.
     * The save area is at [fp - va_save_offset] .. [fp - va_save_offset + 56].
     * Slot i holds xi at offset i*8 from the base.
     */
    if (n->ty && n->ty->is_variadic && va_save_offset > 0) {
        int ri;
        int base;

        base = va_save_offset;
        emit_comment("variadic: spill x0-x7 to save area");
        if (base <= 255) {
            for (ri = 0; ri < 8; ri++) {
                emit("str x%d, [x29, #-%d]", ri, base - ri * 8);
            }
        } else {
            emit_sub_fp("x9", base);
            for (ri = 0; ri < 8; ri++) {
                emit("str x%d, [x9, #%d]", ri, ri * 8);
            }
        }

        /* spill v0-v7 to FP save area (16 bytes per slot).
         * Use x9 as base since offsets may exceed 256. */
        if (va_fp_save_offset > 0) {
            emit_comment("variadic: spill v0-v7 to FP save area");
            emit_sub_fp("x9", va_fp_save_offset);
            for (ri = 0; ri < 8; ri++) {
                emit("str q%d, [x9, #%d]", ri, ri * 16);
            }
        }
    }

    /* spill register arguments into their stack slots */
    {
        int gp_idx, fp_idx;
        int stack_arg_off;  /* offset from fp for caller's stack args */
        struct type *sig_param;

        gp_idx = 0;
        fp_idx = 0;
        sig_param = NULL;
        if (n->ty != NULL && n->ty->kind == TY_FUNC) {
            sig_param = n->ty->params;
        }
        stack_arg_off = 16; /* first stack arg is at [fp+16] */
        for (param = n->args; param != NULL; param = param->next) {
            struct type *param_ty;

            param_ty = param->ty;
            if (param_ty == NULL) {
                param_ty = sig_param;
            }
            if (sig_param != NULL) {
                sig_param = sig_param->next;
            }

            if (param->offset <= 0) {
                if (param_ty != NULL && type_is_fp128(param_ty)) {
                    fp_idx++;
                } else if (param_ty != NULL && type_is_flonum(param_ty)) {
                    fp_idx++;
                } else if (param_ty != NULL &&
                           (param_ty->kind == TY_STRUCT ||
                            param_ty->kind == TY_UNION) &&
                           is_hfa(param_ty)) {
                    fp_idx += hfa_member_count(param_ty);
                } else {
                    gp_idx++;
                    if (param_ty != NULL &&
                        (param_ty->kind == TY_STRUCT ||
                         param_ty->kind == TY_UNION) &&
                        param_ty->size > 8 &&
                        param_ty->size <= 16) {
                        gp_idx++;
                    }
                }
                continue;
            }
            if (param_ty != NULL &&
                param_ty->kind == TY_COMPLEX_DOUBLE) {
                if (fp_idx + 1 < 8) {
                    emit_comment("save complex double arg '%s' to [fp, #-%d]",
                                 param->name ? param->name : "?",
                                 param->offset);
                    if (param->offset <= 255) {
                        emit("str d%d, [x29, #-%d]", fp_idx,
                             param->offset);
                        emit("str d%d, [x29, #-%d]", fp_idx + 1,
                             param->offset - 8);
                    } else {
                        emit_sub_fp("x10", param->offset);
                        emit("str d%d, [x10]", fp_idx);
                        emit("str d%d, [x10, #8]", fp_idx + 1);
                    }
                    fp_idx += 2;
                }
            } else if (param_ty != NULL &&
                       param_ty->kind == TY_COMPLEX_FLOAT) {
                if (fp_idx + 1 < 8) {
                    emit_comment("save complex float arg '%s' to [fp, #-%d]",
                                 param->name ? param->name : "?",
                                 param->offset);
                    if (param->offset <= 255) {
                        emit("str s%d, [x29, #-%d]", fp_idx,
                             param->offset);
                        emit("str s%d, [x29, #-%d]", fp_idx + 1,
                             param->offset - 4);
                    } else {
                        emit_sub_fp("x10", param->offset);
                        emit("str s%d, [x10]", fp_idx);
                        emit("str s%d, [x10, #4]", fp_idx + 1);
                    }
                    fp_idx += 2;
                }
            } else if (param_ty != NULL && type_is_fp128(param_ty)) {
                if (fp_idx < 8) {
                    emit_comment("save long double arg '%s' to [fp, #-%d]",
                                 param->name ? param->name : "?",
                                 param->offset);
                    if (param->offset <= 255) {
                        emit("str q%d, [x29, #-%d]", fp_idx,
                             param->offset);
                    } else {
                        emit_sub_fp("x10", param->offset);
                        emit("str q%d, [x10]", fp_idx);
                    }
                    fp_idx++;
                }
            } else if (param_ty != NULL && type_is_flonum(param_ty)) {
                if (fp_idx < 8) {
                    emit_comment("save fp arg '%s' to [fp, #-%d]",
                                 param->name ? param->name : "?",
                                 param->offset);
                    if (param->offset <= 255) {
                        if (param_ty->kind == TY_FLOAT) {
                            emit("str s%d, [x29, #-%d]", fp_idx,
                                 param->offset);
                        } else {
                            emit("str d%d, [x29, #-%d]", fp_idx,
                                 param->offset);
                        }
                    } else {
                        emit_sub_fp("x10", param->offset);
                        if (param_ty->kind == TY_FLOAT) {
                            emit("str s%d, [x10]", fp_idx);
                        } else {
                            emit("str d%d, [x10]", fp_idx);
                        }
                    }
                    fp_idx++;
                }
            } else {
                /* Check for HFA struct: passed in FP regs */
                if (param_ty != NULL &&
                    (param_ty->kind == TY_STRUCT ||
                     param_ty->kind == TY_UNION)) {
                    int hfa_kind;
                    hfa_kind = is_hfa(param_ty);
                    if (hfa_kind) {
                        int hcnt;

                        hcnt = hfa_member_count(param_ty);
                        if (fp_idx + hcnt <= 8) {
                            int mi, msz;

                            msz = hfa_kind;
                            emit_comment("save HFA arg '%s' (%d members) "
                                         "to [fp, #-%d]",
                                         param->name ? param->name : "?",
                                         hcnt, param->offset);
                            if (param->offset <= 4095) {
                                emit("sub x10, x29, #%d", param->offset);
                            } else {
                                emit_load_imm_reg("x10", param->offset);
                                emit("sub x10, x29, x10");
                            }
                            for (mi = 0; mi < hcnt && fp_idx < 8;
                                 mi++, fp_idx++) {
                                if (hfa_kind == 4) {
                                    emit("str s%d, [x10, #%d]",
                                         fp_idx, mi * msz);
                                } else if (hfa_kind == 16) {
                                    emit("str q%d, [x10, #%d]",
                                         fp_idx, mi * msz);
                                } else {
                                        emit("str d%d, [x10, #%d]",
                                             fp_idx, mi * msz);
                                }
                            }
                            continue;
                        }

                        /* HFA fell off the FP register bank.
                         * Copy the whole aggregate from the caller stack. */
                        emit_comment("copy HFA arg '%s' (%d bytes) "
                                     "from stack to [fp, #-%d]",
                                     param->name ? param->name : "?",
                                     param_ty->size, param->offset);
                        if (param->offset > 255) {
                            if (param->offset <= 4095) {
                                emit("sub x10, x29, #%d", param->offset);
                            } else {
                                emit_load_imm_reg("x10",
                                                  param->offset);
                                emit("sub x10, x29, x10");
                            }
                        }
                        {
                            int psz;
                            int ci;
                            int aal;

                            psz = param_ty->size;
                            aal = (hfa_kind == 16) ? 16 : 8;
                            stack_arg_off = align_to(stack_arg_off, aal);
                            for (ci = 0; ci + 8 <= psz; ci += 8) {
                                emit("ldr x9, [x29, #%d]",
                                     stack_arg_off + ci);
                                if (param->offset <= 255) {
                                    emit("str x9, [x29, #-%d]",
                                         param->offset - ci);
                                } else {
                                    emit("str x9, [x10, #%d]", ci);
                                }
                            }
                            if (ci + 4 <= psz) {
                                emit("ldr w9, [x29, #%d]",
                                     stack_arg_off + ci);
                                if (param->offset <= 255) {
                                    emit("str w9, [x29, #-%d]",
                                         param->offset - ci);
                                } else {
                                    emit("str w9, [x10, #%d]", ci);
                                }
                                ci += 4;
                            }
                            for (; ci < psz; ci++) {
                                emit("ldrb w9, [x29, #%d]",
                                     stack_arg_off + ci);
                                if (param->offset <= 255) {
                                    emit("strb w9, [x29, #-%d]",
                                         param->offset - ci);
                                } else {
                                    emit("strb w9, [x10, #%d]", ci);
                                }
                            }
                            stack_arg_off += align_to(psz, aal);
                        }
                        continue;
                    }
                }
                if (gp_idx < 8) {
                    if (param_ty != NULL &&
                        (param_ty->kind == TY_STRUCT ||
                         param_ty->kind == TY_UNION) &&
                        param_ty->size > 16) {
                        /* large struct: x%d is a pointer, copy to stack */
                        int csz;
                        int ci;

                        csz = param_ty->size;
                        emit_comment("copy struct arg '%s' (%d bytes) "
                                     "from ptr x%d to [fp, #-%d]",
                                     param->name ? param->name : "?",
                                     csz, gp_idx, param->offset);
                        /* compute destination base address in x10
                         * to handle large offsets safely */
                        if (param->offset <= 4095) {
                            emit("sub x10, x29, #%d", param->offset);
                        } else {
                            emit_load_imm_reg("x10", param->offset);
                            emit("sub x10, x29, x10");
                        }
                        /* use memcpy-style loop for very large
                         * structs (> 256 bytes) */
                        if (csz > 256) {
                            emit_load_imm_reg("x11", csz);
                            emit("mov x12, x%d", gp_idx);
                            emit("mov x13, x10");
                            emit_label(".Lcopy.%d", new_label());
                            emit("ldrb w9, [x12], #1");
                            emit("strb w9, [x13], #1");
                            emit("subs x11, x11, #1");
                            emit("b.ne .Lcopy.%d", label_count - 1);
                        } else {
                            for (ci = 0; ci + 8 <= csz; ci += 8) {
                                emit("ldr x9, [x%d, #%d]",
                                     gp_idx, ci);
                                emit("str x9, [x10, #%d]", ci);
                            }
                            for (; ci + 4 <= csz; ci += 4) {
                                emit("ldr w9, [x%d, #%d]",
                                     gp_idx, ci);
                                emit("str w9, [x10, #%d]", ci);
                            }
                            for (; ci < csz; ci++) {
                                emit("ldrb w9, [x%d, #%d]",
                                     gp_idx, ci);
                                emit("strb w9, [x10, #%d]", ci);
                            }
                        }
                        gp_idx++;
                        continue;
                    } else if (param_ty != NULL &&
                               (param_ty->kind == TY_STRUCT ||
                                param_ty->kind == TY_UNION) &&
                               param_ty->size > 8) {
                        /* 9-16 byte struct: packed in two regs */
                        int remain;
                        int bi;

                        emit_comment("save struct arg '%s' (%d bytes) "
                                     "to [fp, #-%d]",
                                     param->name ? param->name : "?",
                                     param_ty->size, param->offset);
                        /* compute destination base in x10 so we can
                         * store an exact tail size for 9-15 byte
                         * structs without overrunning into saved fp/lr
                         * when offset is small (e.g. 9-byte arg). */
                        if (param->offset <= 4095) {
                            emit("sub x10, x29, #%d", param->offset);
                        } else {
                            emit_load_imm_reg("x10", param->offset);
                            emit("sub x10, x29, x10");
                        }
                        if (gp_idx + 1 < 8) {
                            emit("str x%d, [x10]", gp_idx);
                            remain = param_ty->size - 8;
                            if (remain == 8) {
                                emit("str x%d, [x10, #8]", gp_idx + 1);
                            } else if (remain == 4) {
                                emit("str w%d, [x10, #8]", gp_idx + 1);
                            } else if (remain == 2) {
                                emit("strh w%d, [x10, #8]", gp_idx + 1);
                            } else if (remain == 1) {
                                emit("strb w%d, [x10, #8]", gp_idx + 1);
                            } else {
                                emit("mov x11, x%d", gp_idx + 1);
                                for (bi = 0; bi < remain; bi++) {
                                    emit("strb w11, [x10, #%d]", 8 + bi);
                                    if (bi + 1 < remain) {
                                        emit("lsr x11, x11, #8");
                                    }
                                }
                            }
                            gp_idx += 2;
                            continue;
                        }
                        /* Not enough GP registers for the whole struct:
                         * use the stack-passed copy path below. */
                        goto stack_arg_copy;
                    } else {
                        /* ordinary GP arg or small struct/union:
                         * materialize xN into the local slot. */
                        int psz;
                        int bi;

                        psz = (param_ty != NULL) ? param_ty->size : 8;
                        emit_comment("save gp arg '%s' (%d bytes) "
                                     "to [fp, #-%d]",
                                     param->name ? param->name : "?",
                                     psz, param->offset);
                        if (param->offset <= 255) {
                            if (psz == 1) {
                                emit("strb w%d, [x29, #-%d]", gp_idx,
                                     param->offset);
                            } else if (psz == 2) {
                                emit("strh w%d, [x29, #-%d]", gp_idx,
                                     param->offset);
                            } else if (psz == 4) {
                                emit("str w%d, [x29, #-%d]", gp_idx,
                                     param->offset);
                            } else if (psz == 8) {
                                emit("str x%d, [x29, #-%d]", gp_idx,
                                     param->offset);
                            } else {
                                emit("mov x11, x%d", gp_idx);
                                for (bi = 0; bi < psz; bi++) {
                                    if (bi > 0) {
                                        emit("lsr x11, x11, #8");
                                    }
                                    emit("strb w11, [x29, #-%d]",
                                         param->offset - bi);
                                }
                            }
                        } else {
                            if (param->offset <= 4095) {
                                emit("sub x10, x29, #%d",
                                     param->offset);
                            } else {
                                emit_load_imm_reg("x10",
                                                  param->offset);
                                emit("sub x10, x29, x10");
                            }
                            if (psz == 1) {
                                emit("strb w%d, [x10]", gp_idx);
                            } else if (psz == 2) {
                                emit("strh w%d, [x10]", gp_idx);
                            } else if (psz == 4) {
                                emit("str w%d, [x10]", gp_idx);
                            } else if (psz == 8) {
                                emit("str x%d, [x10]", gp_idx);
                            } else {
                                emit("mov x11, x%d", gp_idx);
                                for (bi = 0; bi < psz; bi++) {
                                    if (bi > 0) {
                                        emit("lsr x11, x11, #8");
                                    }
                                    emit("strb w11, [x10, #%d]", bi);
                                }
                            }
                        }
                        gp_idx++;
                        continue;
                    }
                } else {
stack_arg_copy:
                    {
                        /* stack-passed argument: copy from caller's
                         * frame at [fp+stack_arg_off] to local slot */
                        int psz;
                        int ci;
                        int aal;

                        if (param_ty != NULL &&
                            (param_ty->kind == TY_STRUCT ||
                             param_ty->kind == TY_UNION) &&
                            param_ty->size > 16) {
                            int csz;

                            csz = param_ty->size;
                            stack_arg_off = align_to(stack_arg_off, 8);
                            emit_comment("copy struct arg '%s' (%d bytes) "
                                         "from stack ptr at [fp, #%d] "
                                         "to [fp, #-%d]",
                                         param->name ? param->name : "?",
                                         csz, stack_arg_off,
                                         param->offset);
                            emit("ldr x11, [x29, #%d]", stack_arg_off);
                            emit_sub_fp("x10", param->offset);
                            if (csz > 256) {
                                emit_load_imm_reg("x12", csz);
                                emit("mov x13, x11");
                                emit("mov x14, x10");
                                emit_label(".Lcopy.%d", new_label());
                                emit("ldrb w9, [x13], #1");
                                emit("strb w9, [x14], #1");
                                emit("subs x12, x12, #1");
                                emit("b.ne .Lcopy.%d", label_count - 1);
                            } else {
                                for (ci = 0; ci + 8 <= csz; ci += 8) {
                                    emit("ldr x9, [x11, #%d]", ci);
                                    emit("str x9, [x10, #%d]", ci);
                                }
                                for (; ci + 4 <= csz; ci += 4) {
                                    emit("ldr w9, [x11, #%d]", ci);
                                    emit("str w9, [x10, #%d]", ci);
                                }
                                for (; ci < csz; ci++) {
                                    emit("ldrb w9, [x11, #%d]", ci);
                                    emit("strb w9, [x10, #%d]", ci);
                                }
                            }
                            stack_arg_off += 8;
                            continue;
                        }

                    psz = (param_ty != NULL) ? param_ty->size : 8;
                    aal = (param_ty != NULL && type_is_fp128(param_ty)) ? 16 : 8;
                    stack_arg_off = align_to(stack_arg_off, aal);
                    emit_comment("copy stack arg '%s' from [fp, #%d]"
                                 " to [fp, #-%d]",
                                 param->name ? param->name : "?",
                                 stack_arg_off, param->offset);
                    /* compute dest base for large offsets */
                    if (param->offset > 255) {
                        if (param->offset <= 4095) {
                            emit("sub x10, x29, #%d",
                                 param->offset);
                        } else {
                            emit_load_imm_reg("x10",
                                              param->offset);
                            emit("sub x10, x29, x10");
                        }
                    }
                    if (psz <= 4) {
                        emit("ldr w9, [x29, #%d]", stack_arg_off);
                        if (param->offset <= 255) {
                            if (psz == 1) {
                                emit("strb w9, [x29, #-%d]",
                                     param->offset);
                            } else if (psz == 2) {
                                emit("strh w9, [x29, #-%d]",
                                     param->offset);
                            } else {
                                emit_store_gp_value_to_local(9, psz,
                                                             param->offset);
                            }
                        } else {
                            if (psz == 1) {
                                emit("strb w9, [x10]");
                            } else if (psz == 2) {
                                emit("strh w9, [x10]");
                            } else {
                                emit_store_gp_value_to_local(9, psz,
                                                             param->offset);
                            }
                        }
                        stack_arg_off += align_to(8, aal);
                    } else {
                        /* copy full struct/value from stack */
                        for (ci = 0; ci + 8 <= psz; ci += 8) {
                            emit("ldr x9, [x29, #%d]",
                                 stack_arg_off + ci);
                            if (param->offset <= 255) {
                                emit("str x9, [x29, #-%d]",
                                     param->offset - ci);
                            } else {
                                emit("str x9, [x10, #%d]", ci);
                            }
                        }
                        if (ci + 4 <= psz) {
                            emit("ldr w9, [x29, #%d]",
                                 stack_arg_off + ci);
                            if (param->offset <= 255) {
                                emit("str w9, [x29, #-%d]",
                                     param->offset - ci);
                            } else {
                                emit("str w9, [x10, #%d]", ci);
                            }
                            ci += 4;
                        }
                        for (; ci < psz; ci++) {
                            emit("ldrb w9, [x29, #%d]",
                                 stack_arg_off + ci);
                            if (param->offset <= 255) {
                                emit("strb w9, [x29, #-%d]",
                                     param->offset - ci);
                            } else {
                                emit("strb w9, [x10, #%d]", ci);
                            }
                        }
                        stack_arg_off += align_to(psz, aal);
                    }
                }
                }
            }
        }
    }

    /* generate function body */
    gen_stmt(n->body);

    /* implicit return 0 for main() (C99 5.1.2.2.3) */
    if (n->name != NULL && strcmp(n->name, "main") == 0) {
        emit("mov x0, #0");
    }

    /* epilogue */
    emit_label(".L.return.%s", n->name);
    if (current_is_leaf) {
        if (stack_size > 0) {
            emit_comment("leaf epilogue");
            if (stack_size <= 4095) {
                emit("add sp, sp, #%d", stack_size);
            } else {
                emit_load_imm_reg("x16", stack_size);
                emit("add sp, sp, x16");
            }
        }
        emit("ret");
    } else {
        emit_comment("epilogue");
        emit("mov sp, x29");
        emit("ldp x29, x30, [sp], #16");
        emit("ret");
    }

    fprintf(out, "\t.size %s, .-%s\n", n->name, n->name);
    if (n->section_name) {
        fprintf(out, "\t.previous\n");
    }

    /* emit function end label for DWARF DW_AT_high_pc */
    if (cc_debug_info) {
        fprintf(out, ".Lfunc_end_%s:\n", n->name);
    }
}

/* ---- global variable emission ---- */

/*
 * p2align_for - return the power-of-two alignment exponent.
 */
static int p2align_for(int alignment)
{
    if (alignment >= 8) {
        return 3;
    }
    if (alignment >= 4) {
        return 2;
    }
    if (alignment >= 2) {
        return 1;
    }
    return 0;
}

/*
 * emit_data_section - emit .data section directive for a global variable.
 */
static void emit_data_section(struct node *n)
{
    if (n->section_name) {
        fprintf(out, "\t.section %s,\"aw\",%%progbits\n",
                n->section_name);
    } else if (cc_data_sections) {
        fprintf(out, "\t.section .data.%s,\"aw\",%%progbits\n",
                n->name);
    } else {
        fprintf(out, "\t.data\n");
    }
}

/*
 * emit_bss_section - emit .bss section directive for a global variable.
 */
static void emit_bss_section(struct node *n)
{
    if (n->section_name) {
        fprintf(out, "\t.section %s,\"aw\",%%progbits\n",
                n->section_name);
    } else if (cc_data_sections) {
        fprintf(out, "\t.section .bss.%s,\"aw\",%%nobits\n",
                n->name);
    } else {
        fprintf(out, "\t.bss\n");
    }
}

/*
 * emit_scalar_val - emit a .byte/.short/.word/.quad for a given size.
 */
static void emit_scalar_val(int sz, long val)
{
    if (sz == 1) {
        fprintf(out, "\t.byte %ld\n", val);
    } else if (sz == 2) {
        fprintf(out, "\t.short %ld\n", val);
    } else if (sz == 4) {
        fprintf(out, "\t.word %ld\n", val);
    } else {
        fprintf(out, "\t.quad %ld\n", val);
    }
}

/*
 * emit_bytes_le - emit exactly nbytes from val in little-endian order.
 * Used for bitfield storage units that may not fill a standard scalar.
 */
static void emit_bytes_le(int nbytes, unsigned long val)
{
    int i;
    for (i = 0; i < nbytes; i++) {
        fprintf(out, "\t.byte %lu\n", (val >> (i * 8)) & 0xff);
    }
}

/*
 * resolve_const_addr - recursively resolve a constant address expression
 * to (base_symbol_name, byte_offset).  Handles nested array subscripts,
 * pointer arithmetic, deref/addr cancellation, and struct member accesses.
 * Returns 1 on success, 0 if the expression cannot be resolved.
 */
static int resolve_const_addr(struct node *e, const char **base_out,
                               long *offset_out)
{
    if (e == NULL) return 0;

    /* base case: global variable */
    if (e->kind == ND_VAR && e->name != NULL) {
        *base_out = e->name;
        *offset_out = 0;
        return 1;
    }

    /* &expr — strip the addr, resolve inner */
    if (e->kind == ND_ADDR && e->lhs != NULL) {
        /* &*expr => expr */
        if (e->lhs->kind == ND_DEREF && e->lhs->lhs != NULL) {
            return resolve_const_addr(e->lhs->lhs, base_out, offset_out);
        }
        /* &var.member */
        if (e->lhs->kind == ND_MEMBER) {
            struct node *n;
            long moff;
            moff = 0;
            n = e->lhs;
            while (n != NULL && n->kind == ND_MEMBER) {
                moff += n->offset;
                n = n->lhs;
            }
            if (n != NULL && resolve_const_addr(n, base_out, offset_out)) {
                *offset_out += moff;
                return 1;
            }
            return 0;
        }
        return resolve_const_addr(e->lhs, base_out, offset_out);
    }

    /* *expr — just pass through (for array decay) */
    if (e->kind == ND_DEREF && e->lhs != NULL) {
        return resolve_const_addr(e->lhs, base_out, offset_out);
    }

    /* expr + N or N + expr (pointer arithmetic) */
    if (e->kind == ND_ADD && e->lhs != NULL && e->rhs != NULL) {
        if (e->rhs->kind == ND_NUM) {
            long off;
            int esz;
            off = e->rhs->val;
            esz = 1;
            if (e->ty != NULL &&
                (e->ty->kind == TY_PTR || e->ty->kind == TY_ARRAY) &&
                e->ty->base != NULL) {
                esz = e->ty->base->size;
            }
            if (resolve_const_addr(e->lhs, base_out, offset_out)) {
                *offset_out += off * esz;
                return 1;
            }
        }
        if (e->lhs->kind == ND_NUM) {
            long off;
            int esz;
            off = e->lhs->val;
            esz = 1;
            if (e->ty != NULL &&
                (e->ty->kind == TY_PTR || e->ty->kind == TY_ARRAY) &&
                e->ty->base != NULL) {
                esz = e->ty->base->size;
            }
            if (resolve_const_addr(e->rhs, base_out, offset_out)) {
                *offset_out += off * esz;
                return 1;
            }
        }
    }

    /* expr - N (pointer subtraction) */
    if (e->kind == ND_SUB && e->lhs != NULL && e->rhs != NULL &&
        e->rhs->kind == ND_NUM) {
        long off;
        int esz;
        off = e->rhs->val;
        esz = 1;
        if (e->ty != NULL &&
            (e->ty->kind == TY_PTR || e->ty->kind == TY_ARRAY) &&
            e->ty->base != NULL) {
            esz = e->ty->base->size;
        }
        if (resolve_const_addr(e->lhs, base_out, offset_out)) {
            *offset_out -= off * esz;
            return 1;
        }
    }

    /* cast — look through */
    if (e->kind == ND_CAST && e->lhs != NULL) {
        return resolve_const_addr(e->lhs, base_out, offset_out);
    }

    /* string literal */
    /* (can't resolve to a named symbol here easily) */

    return 0;
}

/*
 * find_member_base - walk a chain of ND_MEMBER nodes to find the
 * base ND_VAR and accumulate the total byte offset.
 * Returns the base variable name, or NULL if not a simple chain.
 */
static const char *find_member_base(struct node *n, int *offset_out)
{
    int total;
    total = 0;
    while (n != NULL && n->kind == ND_MEMBER) {
        total += n->offset;
        n = n->lhs;
    }
    if (n != NULL && n->kind == ND_VAR && n->name != NULL) {
        *offset_out = total;
        return n->name;
    }
    return NULL;
}

/*
 * eval_init_numeric - evaluate simple numeric constant expressions used
 * in static initializers.  Returns 1 on success and stores result in out.
 */
static int eval_init_numeric(struct node *e, long double *out_val)
{
    long double lv;
    long double rv;

    if (e == NULL || out_val == NULL) {
        return 0;
    }

    if (e->kind == ND_NUM) {
        *out_val = (long double)e->val;
        return 1;
    }
    if (e->kind == ND_FNUM) {
        *out_val = (long double)e->fval;
        return 1;
    }
    if (e->kind == ND_CAST && e->lhs != NULL) {
        return eval_init_numeric(e->lhs, out_val);
    }
    if (e->kind == ND_SUB && e->lhs != NULL && e->rhs != NULL &&
        e->lhs->kind == ND_NUM && e->lhs->val == 0) {
        if (!eval_init_numeric(e->rhs, &rv)) {
            return 0;
        }
        *out_val = -rv;
        return 1;
    }
    if (e->kind == ND_ADD || e->kind == ND_SUB ||
        e->kind == ND_MUL || e->kind == ND_DIV) {
        if (e->lhs == NULL || e->rhs == NULL) {
            return 0;
        }
        if (!eval_init_numeric(e->lhs, &lv) ||
            !eval_init_numeric(e->rhs, &rv)) {
            return 0;
        }
        if (e->kind == ND_ADD) {
            *out_val = lv + rv;
        } else if (e->kind == ND_SUB) {
            *out_val = lv - rv;
        } else if (e->kind == ND_MUL) {
            *out_val = lv * rv;
        } else {
            *out_val = lv / rv;
        }
        return 1;
    }
    return 0;
}

/*
 * emit_init_expr - emit a constant initializer expression.
 * Returns the number of bytes emitted.
 */
static int emit_init_expr_typed(struct node *e, int member_sz,
                                struct type *mty);

static void emit_struct_init(struct node *ilist, struct type *ty);

static struct node *unwrap_single_init_value(struct node *e)
{
    if (e != NULL && e->kind == ND_INIT_LIST &&
        e->body != NULL && e->body->next == NULL) {
        if (e->body->kind == ND_DESIG_INIT &&
            e->body->lhs != NULL) {
            return e->body->lhs;
        }
        return e->body;
    }
    return e;
}

static int emit_init_expr(struct node *e, int member_sz)
{
    return emit_init_expr_typed(e, member_sz, NULL);
}

static int emit_init_expr_typed(struct node *e, int member_sz,
                                struct type *mty)
{
    if (e == NULL) {
        fprintf(out, "\t.zero %d\n", member_sz);
        return member_sz;
    }

    if (e->kind == ND_INIT_LIST &&
        (mty == NULL || !type_is_aggregate(mty))) {
        e = unwrap_single_init_value(e);
    }

    if (e->kind == ND_COMPOUND_LIT && e->ty != NULL &&
        type_is_aggregate(e->ty) && e->body != NULL &&
        e->body->kind == ND_INIT_LIST) {
        emit_struct_init(e->body, e->ty);
        return member_sz;
    }

    if (e->kind == ND_INIT_LIST && e->ty != NULL &&
        type_is_aggregate(e->ty)) {
        emit_struct_init(e, e->ty);
        return member_sz;
    }

    if (e->kind == ND_NUM) {
        /* integer init for a float/double member: convert */
        if (mty != NULL && mty->kind == TY_FLOAT) {
            float f;
            unsigned int fi;
            f = (float)e->val;
            memcpy(&fi, &f, 4);
            fprintf(out, "\t.word %u\n", fi);
            return 4;
        }
        if (mty != NULL && mty->kind == TY_DOUBLE) {
            double d;
            unsigned long di;
            d = (double)e->val;
            memcpy(&di, &d, 8);
            fprintf(out, "\t.quad %lu\n", di);
            return 8;
        }
        if (mty != NULL && type_is_fp128(mty)) {
            long double ld;
            unsigned long lo, hi;
            ld = (long double)e->val;
            memcpy(&lo, &ld, 8);
            memcpy(&hi, (char *)&ld + 8, 8);
            fprintf(out, "\t.quad %lu\n", lo);
            fprintf(out, "\t.quad %lu\n", hi);
            return 16;
        }
        emit_scalar_val(member_sz, e->val);
        return member_sz;
    }

    if (e->kind == ND_FNUM) {
        if (member_sz == 4) {
            float f;
            unsigned int fi;
            f = (float)e->fval;
            memcpy(&fi, &f, 4);
            fprintf(out, "\t.word %u\n", fi);
        } else if (member_sz >= 16) {
            /* long double: convert double to quad precision */
            long double ld;
            unsigned long lo, hi;
            ld = (long double)e->fval;
            memcpy(&lo, &ld, 8);
            memcpy(&hi, (char *)&ld + 8, 8);
            fprintf(out, "\t.quad %lu\n", lo);
            fprintf(out, "\t.quad %lu\n", hi);
        } else {
            unsigned long di;
            memcpy(&di, &e->fval, 8);
            fprintf(out, "\t.quad %lu\n", di);
        }
        return member_sz;
    }

    /* cast to integer type */
    if (e->kind == ND_CAST && e->lhs != NULL &&
        e->lhs->kind == ND_NUM) {
        emit_scalar_val(member_sz, e->lhs->val);
        return member_sz;
    }

    /* NULL pointer: (void*)0 or (type*)0 */
    if (e->kind == ND_CAST && e->lhs != NULL &&
        e->lhs->kind == ND_NUM && e->lhs->val == 0 &&
        e->ty != NULL && e->ty->kind == TY_PTR) {
        fprintf(out, "\t.quad 0\n");
        return 8;
    }

    /* &global_var — emit a .quad with relocation */
    if (e->kind == ND_ADDR && e->lhs != NULL &&
        e->lhs->kind == ND_VAR && e->lhs->name != NULL) {
        fprintf(out, "\t.quad %s\n", e->lhs->name);
        return 8;
    }

    /* &global_var.member.member... — address of struct member */
    if (e->kind == ND_ADDR && e->lhs != NULL &&
        e->lhs->kind == ND_MEMBER) {
        int moff;
        const char *base;
        base = find_member_base(e->lhs, &moff);
        if (base != NULL) {
            if (moff == 0) {
                fprintf(out, "\t.quad %s\n", base);
            } else {
                fprintf(out, "\t.quad %s + %d\n", base, moff);
            }
            return 8;
        }
    }

    /* &(compound literal) — reference the anonymous static */
    if (e->kind == ND_ADDR && e->lhs != NULL &&
        e->lhs->kind == ND_COMPOUND_LIT) {
        fprintf(out, "\t.quad .LCL%d\n", e->lhs->label_id);
        return 8;
    }

    /* plain global variable reference (pointer/array/function decay) */
    if (e->kind == ND_VAR && e->name != NULL) {
        struct type *ety;

        ety = e->ty != NULL ? e->ty : mty;
        if (ety != NULL &&
            (ety->kind == TY_PTR || ety->kind == TY_ARRAY ||
             ety->kind == TY_FUNC)) {
            fprintf(out, "\t.quad %s\n", e->name);
            return 8;
        }
    }

    /* string literal - reference the .rodata label */
    if (e->kind == ND_STR) {
        fprintf(out, "\t.quad .LS%d\n", e->label_id);
        return 8;
    }

    /* &global_var + offset (e.g. &arr[2], &s->field) */
    if (e->kind == ND_ADDR && e->lhs != NULL &&
        e->lhs->kind == ND_ADD && e->lhs->lhs != NULL &&
        e->lhs->rhs != NULL &&
        e->lhs->lhs->kind == ND_VAR && e->lhs->lhs->name != NULL &&
        e->lhs->rhs->kind == ND_NUM) {
        long off;
        int elem_sz;
        off = e->lhs->rhs->val;
        /* scale by pointer element size if this is ptr arith */
        elem_sz = 1;
        if (e->lhs->ty != NULL &&
            (e->lhs->ty->kind == TY_PTR ||
             e->lhs->ty->kind == TY_ARRAY) &&
            e->lhs->ty->base != NULL) {
            elem_sz = e->lhs->ty->base->size;
        }
        off *= elem_sz;
        if (off == 0) {
            fprintf(out, "\t.quad %s\n", e->lhs->lhs->name);
        } else {
            fprintf(out, "\t.quad %s + %ld\n",
                    e->lhs->lhs->name, off);
        }
        return 8;
    }

    /* &str_literal[N] or &(str + N) or &var[N] */
    if (e->kind == ND_ADDR && e->lhs != NULL) {
        /* addr of deref of (expr + N) */
        struct node *inner;
        inner = e->lhs;
        if (inner->kind == ND_DEREF && inner->lhs != NULL) {
            inner = inner->lhs;
        }
        if (inner->kind == ND_ADD) {
            if (inner->lhs != NULL && inner->lhs->kind == ND_STR &&
                inner->rhs != NULL && inner->rhs->kind == ND_NUM) {
                fprintf(out, "\t.quad .LS%d + %ld\n",
                        inner->lhs->label_id, inner->rhs->val);
                return 8;
            }
            if (inner->rhs != NULL && inner->rhs->kind == ND_STR &&
                inner->lhs != NULL && inner->lhs->kind == ND_NUM) {
                fprintf(out, "\t.quad .LS%d + %ld\n",
                        inner->rhs->label_id, inner->lhs->val);
                return 8;
            }
            /* &var[N] - global variable + constant offset */
            if (inner->lhs != NULL && inner->lhs->kind == ND_VAR &&
                inner->lhs->name != NULL &&
                inner->rhs != NULL && inner->rhs->kind == ND_NUM) {
                long off;
                int esz;
                off = inner->rhs->val;
                /* scale by element size for pointer arithmetic */
                esz = 1;
                if (inner->ty != NULL &&
                    (inner->ty->kind == TY_PTR ||
                     inner->ty->kind == TY_ARRAY) &&
                    inner->ty->base != NULL) {
                    esz = inner->ty->base->size;
                }
                off *= esz;
                if (off == 0) {
                    fprintf(out, "\t.quad %s\n", inner->lhs->name);
                } else {
                    fprintf(out, "\t.quad %s + %ld\n",
                            inner->lhs->name, off);
                }
                return 8;
            }
            if (inner->rhs != NULL && inner->rhs->kind == ND_VAR &&
                inner->rhs->name != NULL &&
                inner->lhs != NULL && inner->lhs->kind == ND_NUM) {
                long off;
                int esz;
                off = inner->lhs->val;
                esz = 1;
                if (inner->ty != NULL &&
                    (inner->ty->kind == TY_PTR ||
                     inner->ty->kind == TY_ARRAY) &&
                    inner->ty->base != NULL) {
                    esz = inner->ty->base->size;
                }
                off *= esz;
                if (off == 0) {
                    fprintf(out, "\t.quad %s\n", inner->rhs->name);
                } else {
                    fprintf(out, "\t.quad %s + %ld\n",
                            inner->rhs->name, off);
                }
                return 8;
            }
        }
        /* &var where inner is a plain global variable */
        if (inner->kind == ND_VAR && inner->name != NULL) {
            fprintf(out, "\t.quad %s\n", inner->name);
            return 8;
        }
        /* &str[0] where str is just the string node */
        if (inner->kind == ND_STR) {
            fprintf(out, "\t.quad .LS%d\n", inner->label_id);
            return 8;
        }
    }

    /* string + N (pointer arithmetic on string literal) */
    if (e->kind == ND_ADD && e->lhs != NULL && e->rhs != NULL) {
        if (e->lhs->kind == ND_STR && e->rhs->kind == ND_NUM) {
            fprintf(out, "\t.quad .LS%d + %ld\n",
                    e->lhs->label_id, e->rhs->val);
            return 8;
        }
        if (e->rhs->kind == ND_STR && e->lhs->kind == ND_NUM) {
            fprintf(out, "\t.quad .LS%d + %ld\n",
                    e->rhs->label_id, e->lhs->val);
            return 8;
        }
    }

    /* &&label -- address of a label (for computed goto tables) */
    if (e->kind == ND_LABEL_ADDR && e->name != NULL) {
        const char *fn;
        fn = e->section_name ? e->section_name : current_func;
        if (fn != NULL) {
            fprintf(out, "\t.quad .L.label.%s.%s\n", fn, e->name);
        } else {
            fprintf(out, "\t.quad 0\n");
        }
        return 8;
    }

    /* cast of an address expression: (T*)&expr or (T*)(expr+N) */
    if (e->kind == ND_CAST && e->lhs != NULL) {
        return emit_init_expr(e->lhs, member_sz);
    }

    /* &global_var.member (ND_ADDR of ND_MEMBER) */
    if (e->kind == ND_ADDR && e->lhs != NULL &&
        e->lhs->kind == ND_MEMBER) {
        /* walk through the member chain to find base var and
         * accumulate total offset */
        struct node *base;
        long total_off;
        total_off = e->lhs->offset;
        base = e->lhs->lhs;
        /* handle ptr->member: ND_DEREF(ND_ADD(VAR, NUM)) */
        if (base != NULL && base->kind == ND_DEREF &&
            base->lhs != NULL) {
            base = base->lhs;
        }
        /* handle arr[N]: ND_ADD(VAR, NUM*scale) */
        if (base != NULL && base->kind == ND_ADD &&
            base->rhs != NULL && base->rhs->kind == ND_NUM &&
            base->lhs != NULL && base->lhs->kind == ND_VAR &&
            base->lhs->name != NULL) {
            long idx_off;
            int esz;
            idx_off = base->rhs->val;
            /* scale by element size for pointer arithmetic */
            esz = 1;
            if (base->ty != NULL &&
                (base->ty->kind == TY_PTR ||
                 base->ty->kind == TY_ARRAY) &&
                base->ty->base != NULL) {
                esz = base->ty->base->size;
            }
            total_off += idx_off * esz;
            base = base->lhs;
        }
        if (base != NULL && base->kind == ND_VAR &&
            base->name != NULL) {
            if (total_off == 0) {
                fprintf(out, "\t.quad %s\n", base->name);
            } else {
                fprintf(out, "\t.quad %s + %ld\n",
                        base->name, total_off);
            }
            return 8;
        }
    }

    /* global_var + N (pointer to global + offset) */
    if (e->kind == ND_ADD && e->lhs != NULL && e->rhs != NULL) {
        if (e->lhs->kind == ND_VAR && e->lhs->name != NULL &&
            e->rhs->kind == ND_NUM) {
            long off;
            int esz;
            off = e->rhs->val;
            /* scale by element size for pointer arithmetic */
            esz = 1;
            if (e->ty != NULL &&
                (e->ty->kind == TY_PTR ||
                 e->ty->kind == TY_ARRAY) &&
                e->ty->base != NULL) {
                esz = e->ty->base->size;
            }
            off *= esz;
            fprintf(out, "\t.quad %s + %ld\n",
                    e->lhs->name, off);
            return 8;
        }
    }

    /* general catch-all: try to resolve any address expression
     * using the recursive resolver */
    {
        const char *rbase;
        long roff;
        rbase = NULL;
        roff = 0;
        if (resolve_const_addr(e, &rbase, &roff) && rbase != NULL) {
            if (roff == 0) {
                fprintf(out, "\t.quad %s\n", rbase);
            } else {
                fprintf(out, "\t.quad %s + %ld\n", rbase, roff);
            }
            return 8;
        }
    }

    /* complex constant: __builtin_complex(re, im) at init time */
    if (e->kind == ND_CALL && e->name != NULL &&
        strcmp(e->name, "__builtin_complex") == 0 &&
        e->args != NULL && e->args->next != NULL) {
        double re_val, im_val;
        unsigned long ru, iu;
        re_val = 0.0;
        im_val = 0.0;
        if (e->args->kind == ND_FNUM) re_val = e->args->fval;
        if (e->args->next->kind == ND_FNUM) im_val = e->args->next->fval;
        memcpy(&ru, &re_val, 8);
        memcpy(&iu, &im_val, 8);
        fprintf(out, "\t.quad 0x%lx\n", ru);
        fprintf(out, "\t.quad 0x%lx\n", iu);
        return 16;
    }

    /* complex add/sub of scalar + complex literal for global init */
    if ((e->kind == ND_ADD || e->kind == ND_SUB) &&
        e->ty != NULL &&
        (e->ty->kind == TY_COMPLEX_DOUBLE ||
         e->ty->kind == TY_COMPLEX_FLOAT) &&
        e->lhs != NULL && e->rhs != NULL) {
        double lre, lim, rre, rim, fre, fim;
        unsigned long u0, u1;
        lre = 0.0; lim = 0.0; rre = 0.0; rim = 0.0;
        /* extract lhs */
        if (e->lhs->kind == ND_FNUM) {
            lre = e->lhs->fval;
        } else if (e->lhs->kind == ND_NUM) {
            lre = (double)e->lhs->val;
        } else if (e->lhs->kind == ND_CALL && e->lhs->name &&
                   strcmp(e->lhs->name, "__builtin_complex") == 0 &&
                   e->lhs->args && e->lhs->args->next) {
            if (e->lhs->args->kind == ND_FNUM)
                lre = e->lhs->args->fval;
            if (e->lhs->args->next->kind == ND_FNUM)
                lim = e->lhs->args->next->fval;
        }
        /* extract rhs */
        if (e->rhs->kind == ND_FNUM) {
            rre = e->rhs->fval;
        } else if (e->rhs->kind == ND_NUM) {
            rre = (double)e->rhs->val;
        } else if (e->rhs->kind == ND_CALL && e->rhs->name &&
                   strcmp(e->rhs->name, "__builtin_complex") == 0 &&
                   e->rhs->args && e->rhs->args->next) {
            if (e->rhs->args->kind == ND_FNUM)
                rre = e->rhs->args->fval;
            if (e->rhs->args->next->kind == ND_FNUM)
                rim = e->rhs->args->next->fval;
        }
        if (e->kind == ND_ADD) {
            fre = lre + rre;
            fim = lim + rim;
        } else {
            fre = lre - rre;
            fim = lim - rim;
        }
        if (e->ty->kind == TY_COMPLEX_FLOAT) {
            float f0, f1;
            unsigned int u0f, u1f;
            f0 = (float)fre;
            f1 = (float)fim;
            memcpy(&u0f, &f0, 4);
            memcpy(&u1f, &f1, 4);
            fprintf(out, "\t.word %u\n", u0f);
            fprintf(out, "\t.word %u\n", u1f);
            return 8;
        } else {
            memcpy(&u0, &fre, 8);
            memcpy(&u1, &fim, 8);
            fprintf(out, "\t.quad 0x%lx\n", u0);
            fprintf(out, "\t.quad 0x%lx\n", u1);
            return 16;
        }
    }

    /* constant comparisons (used by static initializers like:
     *   static int x = -1.0 == 0.0;
     * Front-end does not always fold these, so handle simple
     * numeric constant compares here. */
    if ((e->kind == ND_EQ || e->kind == ND_NE ||
         e->kind == ND_LT || e->kind == ND_LE) &&
        e->lhs != NULL && e->rhs != NULL) {
        int ok;
        long res;
        long double a, b;

        ok = 0;
        res = 0;

        /* Extract numeric constants (supports unary minus/casts). */
        if (eval_init_numeric(e->lhs, &a) &&
            eval_init_numeric(e->rhs, &b)) {
            ok = 1;
        }

        if (ok) {
            /* NaN semantics: any ordered compare is false; != is true. */
            int a_nan, b_nan;
            a_nan = (a != a);
            b_nan = (b != b);
            if (a_nan || b_nan) {
                if (e->kind == ND_NE) {
                    res = 1;
                } else {
                    res = 0;
                }
            } else {
                if (e->kind == ND_EQ) res = (a == b);
                else if (e->kind == ND_NE) res = (a != b);
                else if (e->kind == ND_LT) res = (a < b);
                else res = (a <= b);
            }
            emit_scalar_val(member_sz, res);
            return member_sz;
        }
    }

    /* fallback: emit zero */
    fprintf(out, "\t.zero %d\n", member_sz);
    return member_sz;
}

/* forward declaration for mutual recursion */
static void emit_struct_init(struct node *ilist, struct type *ty);
static void emit_string_literal_bytes(struct node *str, int total_sz);

/*
 * has_desig_init - check if an init list contains ND_DESIG_INIT nodes.
 */
static int has_desig_init(struct node *ilist)
{
    struct node *cur;

    if (ilist == NULL) return 0;
    for (cur = ilist->body; cur != NULL; cur = cur->next) {
        if (cur->kind == ND_DESIG_INIT) return 1;
    }
    return 0;
}

static int has_positional_init(struct node *ilist)
{
    struct node *cur;

    if (ilist == NULL || ilist->kind != ND_INIT_LIST) {
        return 0;
    }
    for (cur = ilist->body; cur != NULL; cur = cur->next) {
        if (cur->kind != ND_DESIG_INIT) {
            return 1;
        }
    }
    return 0;
}

static int has_desig_init_for_type(struct node *ilist, struct type *ty)
{
    struct node *cur;
    struct member *m;

    if (ilist == NULL || ilist->kind != ND_INIT_LIST || ty == NULL) {
        return 0;
    }
    for (cur = ilist->body; cur != NULL; cur = cur->next) {
        if (cur->kind != ND_DESIG_INIT || cur->name == NULL) {
            continue;
        }
        m = type_find_member(ty, cur->name);
        if (m != NULL) {
            return 1;
        }
    }
    return 0;
}

/*
 * emit_desig_value - emit data for a designated init value node.
 */
static void emit_desig_value(struct node *val, struct type *mty,
                             int member_sz)
{
    if (val == NULL) {
        fprintf(out, "\t.zero %d\n", member_sz);
        return;
    }
    if (val->kind == ND_INIT_LIST && mty != NULL &&
        (mty->kind == TY_STRUCT || mty->kind == TY_UNION ||
         mty->kind == TY_ARRAY)) {
        emit_struct_init(val, mty);
    } else if (string_literal_matches_type(mty, val)) {
        emit_string_literal_bytes(val, member_sz);
    } else {
        emit_init_expr_typed(val, member_sz, mty);
    }
}

static void emit_string_literal_bytes(struct node *str, int total_sz)
{
    int slen;
    int si;
    int width;

    if (str == NULL || str->kind != ND_STR) {
        fprintf(out, "\t.zero %d\n", total_sz);
        return;
    }

    width = 1;
    if (str->ty != NULL && str->ty->base != NULL &&
        str->ty->base->size > 0) {
        width = str->ty->base->size;
    }
    slen = (int)str->val;
    for (si = 0; si < slen && si < total_sz; si++) {
        fprintf(out, "\t.byte %d\n",
                (unsigned char)str->name[si]);
    }
    while (width > 0 && si < total_sz) {
        fprintf(out, "\t.byte 0\n");
        si++;
        width--;
    }
    if (si < total_sz) {
        fprintf(out, "\t.zero %d\n", total_sz - si);
    }
}

/*
 * emit_struct_init_r - recursively emit a non-designated initializer
 * sequence for a struct/union/array. Returns the next unconsumed node.
 */
static struct node *emit_struct_init_r(struct node *elem,
                                       struct type *ty,
                                       int base_off)
{
    struct member *m;
    struct node *cur;
    struct node *val;
    int pos;
    int member_sz;
    int pad;

    if (elem == NULL || ty == NULL) {
        return elem;
    }

    cur = elem;
    pos = 0;

    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        if (ty->members == NULL) {
            return (cur != NULL) ? cur->next : cur;
        }
        for (m = ty->members; m != NULL && cur != NULL; m = m->next) {
            int off;

            off = base_off + m->offset;
            if (m->offset > pos) {
                pad = m->offset - pos;
                fprintf(out, "\t.zero %d\n", pad);
                pos = m->offset;
            }
            member_sz = (m->ty != NULL) ? m->ty->size : 4;
            if (m->ty != NULL && type_is_aggregate(m->ty)) {
                if (cur->kind == ND_INIT_LIST) {
                    emit_struct_init(cur, m->ty);
                    cur = cur->next;
                } else {
                    cur = emit_struct_init_r(cur, m->ty, off);
                }
            } else {
                val = cur;
                if (val->kind == ND_INIT_LIST) {
                    val = unwrap_single_init_value(val);
                } else if (val->kind == ND_DESIG_INIT &&
                           val->lhs != NULL) {
                    val = val->lhs;
                }
                emit_init_expr_typed(val, member_sz, m->ty);
                cur = cur->next;
            }
            pos += member_sz;
            if (ty->kind == TY_UNION) {
                break;
            }
        }
        if (pos < ty->size) {
            fprintf(out, "\t.zero %d\n", ty->size - pos);
        }
        return cur;
    }

    if (ty->kind == TY_ARRAY && ty->base != NULL) {
        int idx;
        int esz;

        if (string_literal_matches_type(ty, cur)) {
            emit_string_literal_bytes(cur, ty->size);
            return cur->next;
        }

        esz = ty->base->size;
        idx = 0;
        while (cur != NULL && idx < ty->array_len) {
            int off;

            off = base_off + idx * esz;
            if (string_literal_matches_type(ty, cur)) {
                emit_string_literal_bytes(cur, ty->size);
                return cur->next;
            }
            if (type_is_aggregate(ty->base)) {
                if (cur->kind == ND_INIT_LIST) {
                    emit_struct_init(cur, ty->base);
                    cur = cur->next;
                } else {
                    cur = emit_struct_init_r(cur, ty->base, off);
                }
                idx++;
                pos += esz;
                continue;
            }

            val = cur;
            if (val->kind == ND_INIT_LIST) {
                val = unwrap_single_init_value(val);
            } else if (val->kind == ND_DESIG_INIT &&
                       val->lhs != NULL) {
                val = val->lhs;
            }
            emit_init_expr_typed(val, esz, ty->base);
            cur = cur->next;
            idx++;
            pos += esz;
        }
        if (pos < ty->size) {
            fprintf(out, "\t.zero %d\n", ty->size - pos);
        }
        return cur;
    }

    if (ty->kind == TY_UNION && ty->members != NULL) {
        member_sz = (ty->members->ty != NULL) ? ty->members->ty->size : 4;
        val = cur;
        if (val->kind == ND_INIT_LIST) {
            val = unwrap_single_init_value(val);
        } else if (val->kind == ND_DESIG_INIT && val->lhs != NULL) {
            val = val->lhs;
        }
        if (ty->members->ty != NULL &&
            type_is_aggregate(ty->members->ty)) {
            if (cur->kind == ND_INIT_LIST) {
                emit_struct_init(cur, ty->members->ty);
                cur = cur->next;
            } else {
                cur = emit_struct_init_r(cur, ty->members->ty, base_off);
            }
        } else {
            emit_init_expr_typed(val, member_sz, ty->members->ty);
            cur = cur->next;
        }
        if (member_sz < ty->size) {
            fprintf(out, "\t.zero %d\n", ty->size - member_sz);
        }
        return cur;
    }

    val = elem;
    if (val->kind == ND_INIT_LIST) {
        val = unwrap_single_init_value(val);
    } else if (val->kind == ND_DESIG_INIT && val->lhs != NULL) {
        val = val->lhs;
    }
    emit_init_expr_typed(val, ty->size, ty);
    return elem->next;
}

static void emit_struct_init(struct node *ilist, struct type *ty)
{
    struct member *m;
    struct node *cur;
    int pos;
    int member_sz;
    int pad;

    if (ty == NULL) {
        return;
    }

    cur = (ilist != NULL) ? ilist->body : NULL;
    pos = 0;

    /* designated initializer path for structs/unions:
     * walk members in declaration order, find each member's
     * designated init value from the init list */
    if ((ty->kind == TY_STRUCT || ty->kind == TY_UNION) &&
        has_desig_init(ilist)) {
        struct node *positional;
        unsigned long bf_accum;
        int bf_unit_off;
        int bf_max_bit;
        int bf_emit_sz;
        int in_bitfield;
        int has_positional;
        pos = 0;
        bf_accum = 0;
        bf_unit_off = -1;
        bf_max_bit = 0;
        in_bitfield = 0;
        has_positional = has_positional_init(ilist);
        /* positional tracks non-designated elements in order */
        positional = ilist->body;
        while (positional != NULL &&
               positional->kind == ND_DESIG_INIT) {
            positional = positional->next;
        }
        for (m = ty->members; m != NULL; m = m->next) {
            struct node *found;
            struct node *val;
            found = NULL;
            /* search for a designated init for this member */
            for (cur = ilist->body; cur != NULL;
                 cur = cur->next) {
                if (cur->kind == ND_DESIG_INIT &&
                    cur->name != NULL && m->name != NULL &&
                    strcmp(cur->name, m->name) == 0) {
                    found = cur;
                    break;
                }
            }
            if (found == NULL && m->name == NULL &&
                m->ty != NULL &&
                (m->ty->kind == TY_STRUCT ||
                 m->ty->kind == TY_UNION) &&
                !has_positional &&
                has_desig_init_for_type(ilist, m->ty)) {
                found = ilist;
            }
            if (found == NULL && positional != NULL &&
                positional->kind != ND_DESIG_INIT) {
                found = positional;
                positional = positional->next;
                while (positional != NULL &&
                       positional->kind == ND_DESIG_INIT) {
                    positional = positional->next;
                }
            }
            val = NULL;
            if (found != NULL) {
                val = (found->kind == ND_DESIG_INIT) ?
                    found->lhs : found;
            }
            /* handle bitfield members */
            if (m->bit_width > 0) {
                if (!in_bitfield || m->offset != bf_unit_off) {
                    /* flush previous bitfield unit */
                    if (in_bitfield) {
                        if (bf_unit_off > pos) {
                            fprintf(out, "\t.zero %d\n",
                                    bf_unit_off - pos);
                        }
                        bf_emit_sz = (bf_max_bit + 7) / 8;
                        emit_bytes_le(bf_emit_sz, bf_accum);
                        pos = bf_unit_off + bf_emit_sz;
                    }
                    bf_accum = 0;
                    bf_max_bit = 0;
                    bf_unit_off = m->offset;
                    in_bitfield = 1;
                }
                if (m->bit_offset + m->bit_width > bf_max_bit) {
                    bf_max_bit = m->bit_offset + m->bit_width;
                }
                /* anonymous bitfields don't get values */
                if (m->name != NULL && val != NULL &&
                    val->kind == ND_NUM) {
                    unsigned long mask;
                    mask = ((unsigned long)1 << m->bit_width)
                           - 1;
                    bf_accum |= ((unsigned long)val->val & mask)
                                << m->bit_offset;
                }
            } else {
                /* flush pending bitfield */
                if (in_bitfield) {
                    if (bf_unit_off > pos) {
                        fprintf(out, "\t.zero %d\n",
                                bf_unit_off - pos);
                    }
                    bf_emit_sz = (bf_max_bit + 7) / 8;
                    emit_bytes_le(bf_emit_sz, bf_accum);
                    pos = bf_unit_off + bf_emit_sz;
                    bf_accum = 0;
                    bf_max_bit = 0;
                    in_bitfield = 0;
                }
                member_sz = (m->ty != NULL) ? m->ty->size : 4;
                if (m->offset > pos) {
                    fprintf(out, "\t.zero %d\n",
                            m->offset - pos);
                    pos = m->offset;
                }
                if (val != NULL) {
                    emit_desig_value(val, m->ty, member_sz);
                } else {
                    fprintf(out, "\t.zero %d\n", member_sz);
                }
                pos += member_sz;
            }
            if (ty->kind == TY_UNION) {
                if (found != NULL) {
                    break;
                }
            }
        }
        /* flush trailing bitfield */
        if (in_bitfield) {
            if (bf_unit_off > pos) {
                fprintf(out, "\t.zero %d\n",
                        bf_unit_off - pos);
            }
            bf_emit_sz = (bf_max_bit + 7) / 8;
            emit_bytes_le(bf_emit_sz, bf_accum);
            pos = bf_unit_off + bf_emit_sz;
        }
        if (pos < ty->size) {
            fprintf(out, "\t.zero %d\n", ty->size - pos);
        }
        return;
    }

    /* designated initializer path for arrays:
     * supports out-of-order indices like {[2]=2, [0]=0, [1]=1}
     * and mixed positional/designated like {5, [2]=2, 3} */
    if (ty->kind == TY_ARRAY && ty->base != NULL &&
        has_desig_init(ilist)) {
        struct node **slots;
        int n_slots;
        int elem_sz;
        int cur_idx;
        int si;

        elem_sz = ty->base->size;
        n_slots = ty->array_len;

        slots = (struct node **)calloc(n_slots,
                                       sizeof(struct node *));
        if (slots == NULL) {
            fprintf(stderr,
                    "gen: out of memory for %d desig slots\n",
                    n_slots);
            return;
        }

        /* assign each init element to its slot */
        cur_idx = 0;
        for (cur = ilist->body; cur != NULL; cur = cur->next) {
            if (cur->kind == ND_DESIG_INIT &&
                cur->name == NULL) {
                cur_idx = (int)cur->val;
                if (cur_idx >= 0 && cur_idx < n_slots)
                    slots[cur_idx] = cur->lhs;
                cur_idx++;
            } else {
                if (cur_idx >= 0 && cur_idx < n_slots)
                    slots[cur_idx] = cur;
                cur_idx++;
            }
        }

        /* emit slots in order, coalescing consecutive zeros */
        pos = 0;
        si = 0;
        while (si < n_slots) {
            if (slots[si] != NULL) {
                if (slots[si]->kind == ND_INIT_LIST &&
                    ty->base->kind == TY_STRUCT) {
                    emit_struct_init(slots[si], ty->base);
                } else {
                    emit_desig_value(slots[si], ty->base,
                                     elem_sz);
                }
                pos += elem_sz;
                si++;
            } else {
                /* coalesce consecutive zero slots */
                int zc;
                zc = 0;
                while (si < n_slots && slots[si] == NULL) {
                    zc++;
                    si++;
                }
                fprintf(out, "\t.zero %d\n", zc * elem_sz);
                pos += zc * elem_sz;
            }
        }
        if (pos < ty->size) {
            fprintf(out, "\t.zero %d\n", ty->size - pos);
        }
        free(slots);
        return;
    }

    if (ty->kind == TY_STRUCT) {
        unsigned long bf_accum;
        int bf_unit_off;
        int bf_max_bit;
        int bf_emit_sz;
        int in_bitfield;

        bf_accum = 0;
        bf_unit_off = -1;
        bf_max_bit = 0;
        in_bitfield = 0;

        for (m = ty->members; m != NULL; m = m->next) {
            if (m->bit_width > 0) {
                /* bitfield member: accumulate value into storage unit */
                if (!in_bitfield || m->offset != bf_unit_off) {
                    /* flush previous bitfield storage unit if any */
                    if (in_bitfield) {
                        if (bf_unit_off > pos) {
                            fprintf(out, "\t.zero %d\n",
                                    bf_unit_off - pos);
                        }
                        bf_emit_sz = (bf_max_bit + 7) / 8;
                        emit_bytes_le(bf_emit_sz, bf_accum);
                        pos = bf_unit_off + bf_emit_sz;
                    }
                    bf_accum = 0;
                    bf_max_bit = 0;
                    bf_unit_off = m->offset;
                    in_bitfield = 1;
                }
                if (m->bit_offset + m->bit_width > bf_max_bit) {
                    bf_max_bit = m->bit_offset + m->bit_width;
                }
                /* anonymous bitfields (no name) don't consume
                 * initializer values */
                if (m->name != NULL) {
                    if (cur != NULL && cur->kind == ND_NUM) {
                        unsigned long mask;
                        mask = ((unsigned long)1 << m->bit_width) - 1;
                        bf_accum |= ((unsigned long)cur->val & mask)
                                    << m->bit_offset;
                    }
                    if (cur != NULL) cur = cur->next;
                }
            } else {
                /* flush any pending bitfield storage unit */
                if (in_bitfield) {
                    if (bf_unit_off > pos) {
                        fprintf(out, "\t.zero %d\n",
                                bf_unit_off - pos);
                    }
                    bf_emit_sz = (bf_max_bit + 7) / 8;
                    emit_bytes_le(bf_emit_sz, bf_accum);
                    pos = bf_unit_off + bf_emit_sz;
                    bf_accum = 0;
                    bf_max_bit = 0;
                    in_bitfield = 0;
                }
                /* regular member */
                if (m->offset > pos) {
                    pad = m->offset - pos;
                    fprintf(out, "\t.zero %d\n", pad);
                    pos += pad;
                }
                member_sz = (m->ty != NULL) ? m->ty->size : 4;
                /* flexible array member (size 0): compute
                 * effective size from initializer data */
                if (member_sz == 0 && m->ty != NULL &&
                    m->ty->kind == TY_ARRAY && cur != NULL) {
                    if (cur->kind == ND_STR) {
                        member_sz = (int)cur->val +
                            string_literal_elem_size(cur);
                    } else if (cur->kind == ND_INIT_LIST) {
                        int fac;
                        struct node *fe;
                        fac = 0;
                        for (fe = cur->body; fe; fe = fe->next)
                            fac++;
                        member_sz = fac *
                            ((m->ty->base) ? m->ty->base->size : 1);
                    }
                }
                if (cur != NULL) {
                    if (m->ty != NULL &&
                        type_is_aggregate(m->ty)) {
                        if (cur->kind == ND_INIT_LIST) {
                            emit_struct_init(cur, m->ty);
                            cur = cur->next;
                        } else {
                            cur = emit_struct_init_r(cur, m->ty,
                                                     m->offset);
                        }
                    } else if (cur->kind == ND_INIT_LIST) {
                        emit_init_expr_typed(
                            unwrap_single_init_value(cur),
                            member_sz, m->ty);
                        cur = cur->next;
                    } else if (string_literal_matches_type(m->ty, cur)) {
                        /* string literal initializer for array:
                         * emit bytes inline, then zero-pad */
                        int slen;
                        int si;
                        slen = (int)cur->val +
                            string_literal_elem_size(cur);
                        for (si = 0; si < slen && si < member_sz;
                             si++) {
                            fprintf(out, "\t.byte %d\n",
                                    (unsigned char)cur->name[si]);
                        }
                        /* null terminator */
                        if (si < member_sz) {
                            fprintf(out, "\t.byte 0\n");
                            si++;
                        }
                        /* zero-pad remainder */
                        if (si < member_sz) {
                            fprintf(out, "\t.zero %d\n",
                                    member_sz - si);
                        }
                        cur = cur->next;
                    } else if (m->ty != NULL &&
                               m->ty->kind == TY_ARRAY &&
                               m->ty->base != NULL &&
                               cur->kind != ND_INIT_LIST) {
                        /* flat initializer for array member:
                         * consume multiple scalar values */
                        int elem_sz;
                        int apos;
                        elem_sz = m->ty->base->size;
                        apos = 0;
                        while (cur != NULL && apos < member_sz) {
                            if (cur->kind == ND_INIT_LIST) break;
                            emit_init_expr(cur, elem_sz);
                            apos += elem_sz;
                            cur = cur->next;
                        }
                        if (apos < member_sz) {
                            fprintf(out, "\t.zero %d\n",
                                    member_sz - apos);
                        }
                        /* skip the cur = cur->next below since
                         * we already advanced cur */
                        pos += member_sz;
                        continue;
                    } else {
                        emit_init_expr_typed(cur, member_sz,
                                             m->ty);
                        cur = cur->next;
                    }
                } else {
                    fprintf(out, "\t.zero %d\n", member_sz);
                }
                pos += member_sz;
            }
        }
        /* flush trailing bitfield storage unit */
        if (in_bitfield) {
            if (bf_unit_off > pos) {
                fprintf(out, "\t.zero %d\n", bf_unit_off - pos);
            }
            bf_emit_sz = (bf_max_bit + 7) / 8;
            emit_bytes_le(bf_emit_sz, bf_accum);
            pos = bf_unit_off + bf_emit_sz;
        }
        if (pos < ty->size) {
            fprintf(out, "\t.zero %d\n", ty->size - pos);
        }
    } else if (ty->kind == TY_ARRAY && ty->base != NULL) {
        member_sz = ty->base->size;
        if (string_literal_matches_type(ty, cur)) {
            emit_string_literal_bytes(cur, ty->size);
            return;
        }
        while (cur != NULL) {
            if (cur->kind == ND_INIT_LIST &&
                type_is_aggregate(ty->base)) {
                emit_struct_init(cur, ty->base);
                cur = cur->next;
            } else if (type_is_aggregate(ty->base)) {
                cur = emit_struct_init_r(cur, ty->base, pos);
            } else if (string_literal_matches_type(ty->base, cur)) {
                /* string initializer for sub-array:
                 * emit bytes inline, then zero-pad */
                int slen;
                int si;
                slen = (int)cur->val +
                    string_literal_elem_size(cur);
                for (si = 0; si < slen && si < member_sz; si++) {
                    fprintf(out, "\t.byte %d\n",
                            (unsigned char)cur->name[si]);
                }
                /* null terminator */
                if (si < member_sz) {
                    fprintf(out, "\t.byte 0\n");
                    si++;
                }
                /* zero-pad remainder */
                if (si < member_sz) {
                fprintf(out, "\t.zero %d\n", member_sz - si);
                }
            } else {
                emit_init_expr_typed(cur, member_sz, ty->base);
            }
            pos += member_sz;
            if (!type_is_aggregate(ty->base)) {
                cur = cur->next;
            }
        }
        if (pos < ty->size) {
            fprintf(out, "\t.zero %d\n", ty->size - pos);
        }
    } else if (ty->kind == TY_UNION) {
        member_sz = 0;
        if (cur != NULL && ty->members != NULL) {
            struct type *mty;
            mty = ty->members->ty;
            member_sz = (mty != NULL) ? mty->size : 4;
            if (mty != NULL && type_is_aggregate(mty)) {
                if (cur->kind == ND_INIT_LIST) {
                    emit_struct_init(cur, mty);
                } else {
                    (void)emit_struct_init_r(cur, mty, 0);
                }
            } else if (string_literal_matches_type(mty, cur)) {
                /* string init for char array in union */
                int slen;
                int si;
                slen = (int)cur->val + string_literal_elem_size(cur);
                for (si = 0; si < slen && si < member_sz; si++) {
                    fprintf(out, "\t.byte %d\n",
                            (unsigned char)cur->name[si]);
                }
                if (si < member_sz) {
                    fprintf(out, "\t.byte 0\n");
                    si++;
                }
                if (si < member_sz) {
                    fprintf(out, "\t.zero %d\n", member_sz - si);
                }
            } else {
                emit_init_expr_typed(unwrap_single_init_value(cur),
                                     member_sz, mty);
            }
        }
        if (member_sz < ty->size) {
            fprintf(out, "\t.zero %d\n", ty->size - member_sz);
        }
    } else {
        if (cur != NULL) {
            emit_init_expr(cur, ty->size);
        } else {
            fprintf(out, "\t.zero %d\n", ty->size);
        }
    }
}

/*
 * gen_globalvar - emit a global variable definition in .data or .bss.
 */
static void gen_globalvar(struct node *n)
{
    int sz;
    int has_init;

    if (n == NULL || n->name == NULL) {
        return;
    }

    sz = (n->ty != NULL) ? n->ty->size : 8;
    has_init = (n->val != 0) || (n->init != NULL);

    /* Handle compound literal initializers:
     * &(struct S){1, 2} => emit anonymous static, reference it */
    if (n->init != NULL && n->init->kind == ND_ADDR &&
        n->init->lhs != NULL &&
        n->init->lhs->kind == ND_COMPOUND_LIT) {
        struct node *cl;
        int cl_lbl;

        cl = n->init->lhs;
        cl_lbl = new_label();
        /* emit the compound literal as an anonymous static */
        fprintf(out, "\n\t.local .LCL%d\n", cl_lbl);
        fprintf(out, "\t.data\n");
        if (cl->ty != NULL && cl->ty->align > 1) {
            fprintf(out, "\t.p2align %d\n",
                    p2align_for(cl->ty->align));
        }
        fprintf(out, ".LCL%d:\n", cl_lbl);
        if (cl->body != NULL &&
            cl->body->kind == ND_INIT_LIST) {
            emit_struct_init(cl->body, cl->ty);
        } else {
            fprintf(out, "\t.zero %d\n",
                    cl->ty ? cl->ty->size : 8);
        }
        cl->label_id = cl_lbl;
    }

    fprintf(out, "\n");
    if (n->attr_flags & GEN_ATTR_WEAK) {
        fprintf(out, "\t.weak %s\n", n->name);
    } else if (n->is_static) {
        fprintf(out, "\t.local %s\n", n->name);
    } else {
        fprintf(out, "\t.global %s\n", n->name);
    }

    if (has_init) {
        emit_data_section(n);
        if (n->ty != NULL && n->ty->align > 1) {
            fprintf(out, "\t.p2align %d\n",
                    p2align_for(n->ty->align));
        }
        emit_label("%s", n->name);

        if (n->init != NULL && n->init->kind == ND_INIT_LIST) {
            emit_struct_init(n->init, n->ty);
        } else if (string_literal_matches_type(n->ty, n->init)) {
            /* array initialized by string literal:
             * emit bytes inline, zero-fill remainder */
            int slen, ci;
            const char *s;
            s = n->init->name ? n->init->name : "";
            slen = (int)n->init->val +
                string_literal_elem_size(n->init);
            if (slen > sz) slen = sz;
            for (ci = 0; ci < slen; ci++) {
                fprintf(out, "\t.byte %d\n",
                        (unsigned char)s[ci]);
            }
            for (; ci < sz; ci++) {
                fprintf(out, "\t.byte 0\n");
            }
        } else if (n->init != NULL) {
            emit_init_expr(n->init, sz);
        } else if (n->ty != NULL &&
                   n->ty->kind == TY_DOUBLE) {
            /* emit double as IEEE 754 bit pattern */
            union { double d; long u; } fpun;
            fpun.d = (n->fval != 0.0) ? n->fval : (double)n->val;
            fprintf(out, "\t.quad 0x%lx\n", fpun.u);
        } else if (n->ty != NULL &&
                   type_is_fp128(n->ty)) {
            /* emit long double as IEEE 754 binary128 */
            long double ld;
            unsigned long lo, hi;
            ld = (n->fval != 0.0) ?
                (long double)n->fval : (long double)n->val;
            memcpy(&lo, &ld, 8);
            memcpy(&hi, (char *)&ld + 8, 8);
            fprintf(out, "\t.quad 0x%lx\n", lo);
            fprintf(out, "\t.quad 0x%lx\n", hi);
        } else if (n->ty != NULL &&
                   n->ty->kind == TY_FLOAT) {
            /* emit float as IEEE 754 bit pattern */
            union { float f; int u; } fpun;
            fpun.f = (n->fval != 0.0f) ?
                (float)n->fval : (float)n->val;
            fprintf(out, "\t.word 0x%x\n", fpun.u);
        } else {
            emit_scalar_val(sz, n->val);
        }
    } else {
        emit_bss_section(n);
        if (n->ty != NULL && n->ty->align > 1) {
            fprintf(out, "\t.p2align %d\n",
                    p2align_for(n->ty->align));
        }
        emit_label("%s", n->name);
        fprintf(out, "\t.zero %d\n", sz);
    }
}

/* ---- public interface ---- */

/*
 * gen_set_source_info - set the source file name and directory for debug info.
 * Must be called before gen() when -g is enabled.
 */
void gen_set_source_info(const char *filename, const char *comp_dir)
{
    gen_filename = filename;
    gen_comp_dir = comp_dir;
}

/*
 * gen - generate AArch64 assembly from the AST.
 * prog is a linked list of top-level nodes (ND_FUNCDEF, ND_VAR).
 * Assembly is written to the given FILE.
 */
void gen(struct node *prog, FILE *outfile)
{
    struct node *n;
    int need_rodata;

    out = outfile;
    label_count = 0;
    fp_literal_count = 0;
    current_break_label = -1;
    current_continue_label = -1;

    /* initialize DWARF if debug info enabled */
    if (cc_debug_info) {
        dwarf_init();
    }

    /* file header */
    fprintf(out, "/* generated by free-cc */\n");
    fprintf(out, "\t.arch armv8-a\n");

    /* emit string literals in .rodata if any exist.
     * Check for ND_FUNCDEF (functions may contain strings),
     * ND_STR (top-level strings), and ND_VAR (global variables
     * whose initializers may reference string literals). */
    need_rodata = 0;
    for (n = prog; n != NULL; n = n->next) {
        if (n->kind == ND_FUNCDEF || n->kind == ND_STR
            || n->kind == ND_VAR) {
            need_rodata = 1;
            break;
        }
    }
    if (need_rodata) {
        fprintf(out, "\n\t.section .rodata\n");
        collect_all_strings(prog);
    }

    /* emit global variables (handle tentative definitions:
     * if the same name appears multiple times, prefer the
     * one with an initializer, otherwise emit only the last) */
    for (n = prog; n != NULL; n = n->next) {
        struct node *dup;
        int skip;

        if (n->kind != ND_VAR || n->offset != 0)
            continue;
        if (n->name == NULL)
            continue;

        skip = 0;
        /* tentative definition: skip if another definition
         * of the same name has an initializer */
        if (n->val == 0 && n->init == NULL) {
            for (dup = prog; dup != NULL; dup = dup->next) {
                if (dup == n) continue;
                if (dup->kind != ND_VAR || dup->offset != 0)
                    continue;
                if (dup->name == NULL ||
                    strcmp(dup->name, n->name) != 0)
                    continue;
                if (dup->val != 0 || dup->init != NULL) {
                    skip = 1;
                    break;
                }
            }
            /* all tentative: pick the one with the largest type
             * (handles int Array[]; followed by int Array[32];) */
            if (!skip) {
                int my_sz;
                my_sz = (n->ty != NULL) ? n->ty->size : 0;
                for (dup = prog; dup != NULL; dup = dup->next) {
                    int dup_sz;
                    if (dup == n) continue;
                    if (dup->kind != ND_VAR || dup->offset != 0)
                        continue;
                    if (dup->name == NULL ||
                        strcmp(dup->name, n->name) != 0)
                        continue;
                    dup_sz = (dup->ty != NULL) ? dup->ty->size : 0;
                    if (dup_sz > my_sz) {
                        skip = 1;
                        break;
                    }
                    /* same size: skip if not the first */
                    if (dup_sz == my_sz && dup != n) {
                        /* check if dup comes before n */
                        struct node *p;
                        for (p = prog; p != NULL; p = p->next) {
                            if (p == dup) { skip = 1; break; }
                            if (p == n) break;
                        }
                        if (skip) break;
                    }
                }
            }
        }
        if (!skip) {
            gen_globalvar(n);
        }
    }

    /* emit functions */
    fprintf(out, "\n\t.text\n");
    if (cc_debug_info) {
        fprintf(out, ".Ltext0:\n");
    }
    for (n = prog; n != NULL; n = n->next) {
        if (n->kind == ND_FUNCDEF) {
            gen_funcdef(n);
        }
    }
    if (cc_debug_info) {
        fprintf(out, ".Letext0:\n");
    }

    /* emit FP literal pool in .rodata */
    if (fp_literal_count > 0) {
        fprintf(out, "\n\t.section .rodata\n");
        collect_all_fp(prog);
    }

    /* emit .weak directives for undefined weak symbols,
     * and .set directives for alias attributes */
    {
        extern struct symbol *parse_get_globals(void);
        struct symbol *gsym;
        for (gsym = parse_get_globals(); gsym != NULL;
             gsym = gsym->next) {
            if ((gsym->attr_flags & GEN_ATTR_WEAK) &&
                !gsym->is_defined) {
                fprintf(out, "\t.weak %s\n", gsym->name);
            }
            if (gsym->alias_target != NULL) {
                fprintf(out, "\t.global %s\n", gsym->name);
                fprintf(out, "\t.set %s, %s\n",
                        gsym->name, gsym->alias_target);
            }
        }
    }

    /* mark stack as non-executable */
    fprintf(out, "\n\t.section .note.GNU-stack,\"\",%%progbits\n");

    /* emit DWARF debug sections if enabled */
    if (cc_debug_info) {
        dwarf_generate(prog, out, gen_filename, gen_comp_dir);
    }
}
