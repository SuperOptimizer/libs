/*
 * ext_builtins.c - GCC/Clang builtin function handling for free-cc.
 * Recognizes __builtin_* and __atomic_* identifiers, emitting
 * inline code or folding at compile time where possible.
 * Targets aarch64.  Pure C89.  All variables at top of block.
 */

#include "free.h"
#include <string.h>
#include <stdio.h>

/* ---- memory order constants (match GCC) ---- */
#define MO_RELAXED  0
#define MO_CONSUME  1
#define MO_ACQUIRE  2
#define MO_RELEASE  3
#define MO_ACQ_REL  4
#define MO_SEQ_CST  5

/* ---- builtin IDs ---- */
enum builtin_id {
    BI_NONE = 0,
    BI_EXPECT, BI_UNREACHABLE, BI_TRAP,
    BI_CONSTANT_P, BI_OFFSETOF, BI_TYPES_COMPAT, BI_CHOOSE_EXPR,
    BI_CLZ, BI_CLZL, BI_CLZLL,
    BI_CTZ, BI_CTZL, BI_CTZLL,
    BI_POPCOUNT, BI_POPCOUNTL, BI_POPCOUNTLL,
    BI_FFS, BI_FFSL, BI_FFSLL,
    BI_PARITY, BI_PARITYL, BI_PARITYLL,
    BI_BSWAP16, BI_BSWAP32, BI_BSWAP64,
    BI_MEMCPY, BI_MEMSET, BI_STRLEN, BI_STRCMP,
    BI_ATOMIC_LOAD, BI_ATOMIC_STORE, BI_ATOMIC_XCHG,
    BI_ATOMIC_CMP_XCHG,
    BI_ATOMIC_ADD_FETCH, BI_ATOMIC_SUB_FETCH,
    BI_ATOMIC_AND_FETCH, BI_ATOMIC_OR_FETCH, BI_ATOMIC_XOR_FETCH,
    BI_ATOMIC_FETCH_ADD, BI_ATOMIC_FETCH_SUB,
    BI_ATOMIC_FETCH_AND, BI_ATOMIC_FETCH_OR, BI_ATOMIC_FETCH_XOR,
    BI_SYNC_SYNCHRONIZE,
    BI_VA_START, BI_VA_END, BI_VA_ARG, BI_VA_COPY,
    BI_ADD_OVERFLOW, BI_SUB_OVERFLOW, BI_MUL_OVERFLOW,
    BI_FRAME_ADDR, BI_RETURN_ADDR,
    /* math / float builtins */
    BI_SIGNBIT, BI_SIGNBITF,
    BI_COPYSIGN, BI_COPYSIGNF,
    BI_INF, BI_INFF, BI_NAN, BI_NANF,
    BI_HUGE_VAL, BI_HUGE_VALF,
    BI_ISNAN, BI_ISINF, BI_ISFINITE,
    BI_ABS, BI_LABS,
    BI_FABS, BI_FABSF,
    BI_SQRT, BI_SQRTF,
    /* control flow */
    BI_EXPECT_WITH_PROB
};

struct bi_entry { const char *name; enum builtin_id id; };

static const struct bi_entry bi_table[] = {
    {"__builtin_expect",            BI_EXPECT},
    {"__builtin_unreachable",       BI_UNREACHABLE},
    {"__builtin_trap",              BI_TRAP},
    {"__builtin_constant_p",        BI_CONSTANT_P},
    {"__builtin_offsetof",          BI_OFFSETOF},
    {"__builtin_types_compatible_p",BI_TYPES_COMPAT},
    {"__builtin_choose_expr",       BI_CHOOSE_EXPR},
    {"__builtin_clz",    BI_CLZ},    {"__builtin_clzl",   BI_CLZL},
    {"__builtin_clzll",  BI_CLZLL},
    {"__builtin_ctz",    BI_CTZ},    {"__builtin_ctzl",   BI_CTZL},
    {"__builtin_ctzll",  BI_CTZLL},
    {"__builtin_popcount",   BI_POPCOUNT},
    {"__builtin_popcountl",  BI_POPCOUNTL},
    {"__builtin_popcountll", BI_POPCOUNTLL},
    {"__builtin_ffs",    BI_FFS},    {"__builtin_ffsl",   BI_FFSL},
    {"__builtin_ffsll",  BI_FFSLL},
    {"__builtin_parity",   BI_PARITY},
    {"__builtin_parityl",  BI_PARITYL},
    {"__builtin_parityll", BI_PARITYLL},
    {"__builtin_bswap16",  BI_BSWAP16},
    {"__builtin_bswap32",  BI_BSWAP32},
    {"__builtin_bswap64",  BI_BSWAP64},
    {"__builtin_memcpy",   BI_MEMCPY},
    {"__builtin_memset",   BI_MEMSET},
    {"__builtin_strlen",   BI_STRLEN},
    {"__builtin_strcmp",    BI_STRCMP},
    /* atomic load/store/exchange/CAS */
    {"__atomic_load_n",             BI_ATOMIC_LOAD},
    {"__atomic_store_n",            BI_ATOMIC_STORE},
    {"__atomic_exchange_n",         BI_ATOMIC_XCHG},
    {"__atomic_compare_exchange_n", BI_ATOMIC_CMP_XCHG},
    /* __atomic_OP_fetch: return NEW value */
    {"__atomic_add_fetch",          BI_ATOMIC_ADD_FETCH},
    {"__atomic_sub_fetch",          BI_ATOMIC_SUB_FETCH},
    {"__atomic_and_fetch",          BI_ATOMIC_AND_FETCH},
    {"__atomic_or_fetch",           BI_ATOMIC_OR_FETCH},
    {"__atomic_xor_fetch",          BI_ATOMIC_XOR_FETCH},
    /* __atomic_fetch_OP: return OLD value */
    {"__atomic_fetch_add",          BI_ATOMIC_FETCH_ADD},
    {"__atomic_fetch_sub",          BI_ATOMIC_FETCH_SUB},
    {"__atomic_fetch_and",          BI_ATOMIC_FETCH_AND},
    {"__atomic_fetch_or",           BI_ATOMIC_FETCH_OR},
    {"__atomic_fetch_xor",          BI_ATOMIC_FETCH_XOR},
    /* barrier */
    {"__sync_synchronize",          BI_SYNC_SYNCHRONIZE},
    {"__builtin_va_start", BI_VA_START},
    {"__builtin_va_end",   BI_VA_END},
    {"__builtin_va_arg",   BI_VA_ARG},
    {"__builtin_va_copy",  BI_VA_COPY},
    {"__builtin_add_overflow",   BI_ADD_OVERFLOW},
    {"__builtin_sub_overflow",   BI_SUB_OVERFLOW},
    {"__builtin_mul_overflow",   BI_MUL_OVERFLOW},
    {"__builtin_frame_address",  BI_FRAME_ADDR},
    {"__builtin_return_address", BI_RETURN_ADDR},
    /* math / float builtins */
    {"__builtin_signbit",    BI_SIGNBIT},
    {"__builtin_signbitf",   BI_SIGNBITF},
    {"__builtin_copysign",   BI_COPYSIGN},
    {"__builtin_copysignf",  BI_COPYSIGNF},
    {"__builtin_inf",        BI_INF},
    {"__builtin_inff",       BI_INFF},
    {"__builtin_nan",        BI_NAN},
    {"__builtin_nanf",       BI_NANF},
    {"__builtin_huge_val",   BI_HUGE_VAL},
    {"__builtin_huge_valf",  BI_HUGE_VALF},
    {"__builtin_isnan",      BI_ISNAN},
    {"__builtin_isinf",      BI_ISINF},
    {"__builtin_isfinite",   BI_ISFINITE},
    {"__builtin_abs",        BI_ABS},
    {"__builtin_labs",       BI_LABS},
    {"__builtin_fabs",       BI_FABS},
    {"__builtin_fabsf",      BI_FABSF},
    {"__builtin_sqrt",       BI_SQRT},
    {"__builtin_sqrtf",      BI_SQRTF},
    /* control flow */
    {"__builtin_expect_with_probability", BI_EXPECT_WITH_PROB},
    {NULL, BI_NONE}
};

int builtin_lookup(const char *name)
{
    int i;
    if (!name || name[0] != '_') return BI_NONE;
    for (i = 0; bi_table[i].name; i++)
        if (strcmp(name, bi_table[i].name) == 0)
            return (int)bi_table[i].id;
    return BI_NONE;
}

int builtin_is_known(const char *name)
{
    return builtin_lookup(name) != BI_NONE;
}

int builtin_is_compiletime(int id)
{
    return id == (int)BI_CONSTANT_P || id == (int)BI_TYPES_COMPAT ||
           id == (int)BI_OFFSETOF;
}

int builtin_returns_void(int id)
{
    return id == (int)BI_UNREACHABLE || id == (int)BI_TRAP ||
           id == (int)BI_VA_START || id == (int)BI_VA_END ||
           id == (int)BI_VA_COPY || id == (int)BI_SYNC_SYNCHRONIZE ||
           id == (int)BI_ATOMIC_STORE;
}

int builtin_is_atomic(int id)
{
    return id >= (int)BI_ATOMIC_LOAD && id <= (int)BI_ATOMIC_FETCH_XOR;
}

/* ---- aarch64 code emission ---- */

/* emit NEON popcount: input in fpr, result in w0 */
static void emit_neon_popcnt(FILE *out, int is64)
{
    if (is64) fprintf(out, "\tfmov d0, x0\n");
    else      fprintf(out, "\tfmov s0, w0\n");
    fprintf(out, "\tcnt v0.8b, v0.8b\n");
    fprintf(out, "\taddv b0, v0.8b\n");
    fprintf(out, "\tfmov w0, s0\n");
    if (is64) fprintf(out, "\tand x0, x0, #0xff\n");
}

/* emit ffs: ctz+1 with zero check */
static void emit_ffs(FILE *out, int is64)
{
    const char *r;
    r = is64 ? "x0" : "w0";
    fprintf(out, "\tcbz %s, 1f\n", r);
    fprintf(out, "\trbit %s, %s\n", r, r);
    fprintf(out, "\tclz %s, %s\n", r, r);
    fprintf(out, "\tadd %s, %s, #1\n", r, r);
    fprintf(out, "\tb 2f\n1:\n\tmov %s, #0\n2:\n", r);
}

/*
 * emit_atomic_load - __atomic_load_n(ptr, memorder)
 * ptr in x0, memorder is compile-time constant.
 * Result in x0.
 */
static void emit_atomic_load(FILE *out, int memorder)
{
    if (memorder == MO_RELAXED) {
        fprintf(out, "\tldr x0, [x0]\n");
    } else {
        /* ACQUIRE, SEQ_CST, CONSUME all use ldar */
        fprintf(out, "\tldar x0, [x0]\n");
    }
}

/*
 * emit_atomic_store - __atomic_store_n(ptr, val, memorder)
 * ptr in x0, val in x1.
 */
static void emit_atomic_store(FILE *out, int memorder)
{
    if (memorder == MO_RELAXED) {
        fprintf(out, "\tstr x1, [x0]\n");
    } else {
        /* RELEASE, SEQ_CST use stlr */
        fprintf(out, "\tstlr x1, [x0]\n");
    }
}

/*
 * emit_atomic_xchg - __atomic_exchange_n(ptr, val, memorder)
 * ptr in x0, val in x1. Old value returned in x0.
 */
static void emit_atomic_xchg(FILE *out, int memorder)
{
    const char *ldx;
    const char *stx;

    if (memorder == MO_RELAXED) {
        ldx = "ldxr";
        stx = "stxr";
    } else if (memorder == MO_ACQUIRE) {
        ldx = "ldaxr";
        stx = "stxr";
    } else if (memorder == MO_RELEASE) {
        ldx = "ldxr";
        stx = "stlxr";
    } else {
        /* ACQ_REL, SEQ_CST */
        ldx = "ldaxr";
        stx = "stlxr";
    }
    fprintf(out, "1:\n\t%s x2, [x0]\n", ldx);
    fprintf(out, "\t%s w3, x1, [x0]\n", stx);
    fprintf(out, "\tcbnz w3, 1b\n");
    fprintf(out, "\tmov x0, x2\n");
}

/*
 * emit_atomic_cmp_xchg - __atomic_compare_exchange_n
 * ptr in x0, expected ptr in x1, desired in x2.
 * Returns 1 on success, 0 on failure. On failure, *expected updated.
 */
static void emit_atomic_cmp_xchg(FILE *out, int memorder)
{
    const char *ldx;
    const char *stx;

    if (memorder == MO_RELAXED) {
        ldx = "ldxr";
        stx = "stxr";
    } else if (memorder == MO_ACQUIRE) {
        ldx = "ldaxr";
        stx = "stxr";
    } else if (memorder == MO_RELEASE) {
        ldx = "ldxr";
        stx = "stlxr";
    } else {
        ldx = "ldaxr";
        stx = "stlxr";
    }
    fprintf(out, "\tldr x6, [x1]\n");
    fprintf(out, "1:\n\t%s x7, [x0]\n", ldx);
    fprintf(out, "\tcmp x7, x6\n");
    fprintf(out, "\tb.ne 2f\n");
    fprintf(out, "\t%s w8, x2, [x0]\n", stx);
    fprintf(out, "\tcbnz w8, 1b\n");
    fprintf(out, "\tmov x0, #1\n");
    fprintf(out, "\tb 3f\n");
    fprintf(out, "2:\n\tstr x7, [x1]\n");
    fprintf(out, "\tmov x0, #0\n");
    fprintf(out, "3:\n");
}

/*
 * emit_atomic_op_fetch - __atomic_OP_fetch(ptr, val, memorder)
 * ptr in x0, val in x1. Returns NEW value in x0.
 */
static void emit_atomic_op_fetch(FILE *out, const char *op, int memorder)
{
    const char *ldx;
    const char *stx;

    if (memorder == MO_RELAXED) {
        ldx = "ldxr";
        stx = "stxr";
    } else if (memorder == MO_ACQUIRE) {
        ldx = "ldaxr";
        stx = "stxr";
    } else if (memorder == MO_RELEASE) {
        ldx = "ldxr";
        stx = "stlxr";
    } else {
        ldx = "ldaxr";
        stx = "stlxr";
    }
    fprintf(out, "1:\n\t%s x2, [x0]\n", ldx);
    fprintf(out, "\t%s x2, x2, x1\n", op);
    fprintf(out, "\t%s w3, x2, [x0]\n", stx);
    fprintf(out, "\tcbnz w3, 1b\n");
    fprintf(out, "\tmov x0, x2\n");
}

/*
 * emit_atomic_fetch_op - __atomic_fetch_OP(ptr, val, memorder)
 * ptr in x0, val in x1. Returns OLD value in x0.
 */
static void emit_atomic_fetch_op(FILE *out, const char *op, int memorder)
{
    const char *ldx;
    const char *stx;

    if (memorder == MO_RELAXED) {
        ldx = "ldxr";
        stx = "stxr";
    } else if (memorder == MO_ACQUIRE) {
        ldx = "ldaxr";
        stx = "stxr";
    } else if (memorder == MO_RELEASE) {
        ldx = "ldxr";
        stx = "stlxr";
    } else {
        ldx = "ldaxr";
        stx = "stlxr";
    }
    fprintf(out, "1:\n\t%s x2, [x0]\n", ldx);
    fprintf(out, "\t%s x4, x2, x1\n", op);
    fprintf(out, "\t%s w3, x4, [x0]\n", stx);
    fprintf(out, "\tcbnz w3, 1b\n");
    fprintf(out, "\tmov x0, x2\n");
}

/*
 * builtin_emit_atomic - emit code for an atomic builtin with
 * memorder-aware instruction selection.
 * Called from gen.c after evaluating arguments into x0, x1, x2, etc.
 */
void builtin_emit_atomic(FILE *out, int id, int memorder)
{
    switch ((enum builtin_id)id) {
    case BI_ATOMIC_LOAD:
        emit_atomic_load(out, memorder);
        break;
    case BI_ATOMIC_STORE:
        emit_atomic_store(out, memorder);
        break;
    case BI_ATOMIC_XCHG:
        emit_atomic_xchg(out, memorder);
        break;
    case BI_ATOMIC_CMP_XCHG:
        emit_atomic_cmp_xchg(out, memorder);
        break;
    case BI_ATOMIC_ADD_FETCH:
        emit_atomic_op_fetch(out, "add", memorder);
        break;
    case BI_ATOMIC_SUB_FETCH:
        emit_atomic_op_fetch(out, "sub", memorder);
        break;
    case BI_ATOMIC_AND_FETCH:
        emit_atomic_op_fetch(out, "and", memorder);
        break;
    case BI_ATOMIC_OR_FETCH:
        emit_atomic_op_fetch(out, "orr", memorder);
        break;
    case BI_ATOMIC_XOR_FETCH:
        emit_atomic_op_fetch(out, "eor", memorder);
        break;
    case BI_ATOMIC_FETCH_ADD:
        emit_atomic_fetch_op(out, "add", memorder);
        break;
    case BI_ATOMIC_FETCH_SUB:
        emit_atomic_fetch_op(out, "sub", memorder);
        break;
    case BI_ATOMIC_FETCH_AND:
        emit_atomic_fetch_op(out, "and", memorder);
        break;
    case BI_ATOMIC_FETCH_OR:
        emit_atomic_fetch_op(out, "orr", memorder);
        break;
    case BI_ATOMIC_FETCH_XOR:
        emit_atomic_fetch_op(out, "eor", memorder);
        break;
    default:
        break;
    }
}

void builtin_emit(FILE *out, int id, int nargs)
{
    (void)nargs;
    switch ((enum builtin_id)id) {
    case BI_EXPECT:      break; /* x0 = value, ignore hint */
    case BI_UNREACHABLE: fprintf(out, "\tbrk #0x1\n"); break;
    case BI_TRAP:        fprintf(out, "\tbrk #0xf000\n"); break;
    case BI_CONSTANT_P:  fprintf(out, "\tmov x0, #0\n"); break;

    /* CLZ: native aarch64 instruction */
    case BI_CLZ:   fprintf(out, "\tclz w0, w0\n"); break;
    case BI_CLZL:
    case BI_CLZLL: fprintf(out, "\tclz x0, x0\n"); break;

    /* CTZ: rbit + clz */
    case BI_CTZ:
        fprintf(out, "\trbit w0, w0\n\tclz w0, w0\n"); break;
    case BI_CTZL:
    case BI_CTZLL:
        fprintf(out, "\trbit x0, x0\n\tclz x0, x0\n"); break;

    /* POPCOUNT: NEON cnt */
    case BI_POPCOUNT:   emit_neon_popcnt(out, 0); break;
    case BI_POPCOUNTL:
    case BI_POPCOUNTLL: emit_neon_popcnt(out, 1); break;

    /* FFS: ctz+1 with zero check */
    case BI_FFS:  emit_ffs(out, 0); break;
    case BI_FFSL:
    case BI_FFSLL: emit_ffs(out, 1); break;

    /* BSWAP: rev instruction */
    case BI_BSWAP16:
        fprintf(out, "\trev16 w0, w0\n\tand w0, w0, #0xffff\n"); break;
    case BI_BSWAP32: fprintf(out, "\trev w0, w0\n"); break;
    case BI_BSWAP64: fprintf(out, "\trev x0, x0\n"); break;

    /* PARITY: popcount & 1 */
    case BI_PARITY:
        emit_neon_popcnt(out, 0);
        fprintf(out, "\tand w0, w0, #1\n"); break;
    case BI_PARITYL:
    case BI_PARITYLL:
        emit_neon_popcnt(out, 1);
        fprintf(out, "\tand w0, w0, #1\n"); break;

    /* memory builtins -> libc call */
    case BI_MEMCPY:  fprintf(out, "\tbl memcpy\n"); break;
    case BI_MEMSET:  fprintf(out, "\tbl memset\n"); break;
    case BI_STRLEN:  fprintf(out, "\tbl strlen\n"); break;
    case BI_STRCMP:   fprintf(out, "\tbl strcmp\n"); break;

    /* atomics: default SEQ_CST when called through builtin_emit */
    case BI_ATOMIC_LOAD:
        emit_atomic_load(out, MO_SEQ_CST); break;
    case BI_ATOMIC_STORE:
        emit_atomic_store(out, MO_SEQ_CST); break;
    case BI_ATOMIC_XCHG:
        emit_atomic_xchg(out, MO_SEQ_CST); break;
    case BI_ATOMIC_CMP_XCHG:
        emit_atomic_cmp_xchg(out, MO_SEQ_CST); break;
    case BI_ATOMIC_ADD_FETCH:
        emit_atomic_op_fetch(out, "add", MO_SEQ_CST); break;
    case BI_ATOMIC_SUB_FETCH:
        emit_atomic_op_fetch(out, "sub", MO_SEQ_CST); break;
    case BI_ATOMIC_AND_FETCH:
        emit_atomic_op_fetch(out, "and", MO_SEQ_CST); break;
    case BI_ATOMIC_OR_FETCH:
        emit_atomic_op_fetch(out, "orr", MO_SEQ_CST); break;
    case BI_ATOMIC_XOR_FETCH:
        emit_atomic_op_fetch(out, "eor", MO_SEQ_CST); break;
    case BI_ATOMIC_FETCH_ADD:
        emit_atomic_fetch_op(out, "add", MO_SEQ_CST); break;
    case BI_ATOMIC_FETCH_SUB:
        emit_atomic_fetch_op(out, "sub", MO_SEQ_CST); break;
    case BI_ATOMIC_FETCH_AND:
        emit_atomic_fetch_op(out, "and", MO_SEQ_CST); break;
    case BI_ATOMIC_FETCH_OR:
        emit_atomic_fetch_op(out, "orr", MO_SEQ_CST); break;
    case BI_ATOMIC_FETCH_XOR:
        emit_atomic_fetch_op(out, "eor", MO_SEQ_CST); break;
    case BI_SYNC_SYNCHRONIZE:
        fprintf(out, "\tdmb ish\n"); break;

    /* VA builtins handled in parser */
    case BI_VA_START: case BI_VA_END:
    case BI_VA_ARG: case BI_VA_COPY: break;

    /* compile-time only */
    case BI_OFFSETOF: case BI_TYPES_COMPAT: case BI_CHOOSE_EXPR:
        fprintf(out, "\tmov x0, #0\n"); break;

    /* overflow builtins: handled in gen.c via ND_BUILTIN_OVERFLOW */
    case BI_ADD_OVERFLOW: case BI_SUB_OVERFLOW:
    case BI_MUL_OVERFLOW: break;

    /* frame/return address: handled in gen.c via ND_CALL interception */
    case BI_FRAME_ADDR: case BI_RETURN_ADDR: break;

    /* math / float builtins: handled in gen.c */
    case BI_SIGNBIT: case BI_SIGNBITF:
    case BI_COPYSIGN: case BI_COPYSIGNF:
    case BI_INF: case BI_INFF:
    case BI_NAN: case BI_NANF:
    case BI_HUGE_VAL: case BI_HUGE_VALF:
    case BI_ISNAN: case BI_ISINF: case BI_ISFINITE:
    case BI_ABS: case BI_LABS:
    case BI_FABS: case BI_FABSF:
    case BI_SQRT: case BI_SQRTF:
    case BI_EXPECT_WITH_PROB:
        break;

    case BI_NONE: break;
    }
}

/* ---- compile-time constant folding ---- */

int builtin_try_const_fold(int id, long *args, int nargs, long *result)
{
    unsigned long v;
    int count;

    (void)nargs;
    switch ((enum builtin_id)id) {
    case BI_CONSTANT_P: *result = 0; return 1;
    case BI_EXPECT:
        if (nargs >= 1) { *result = args[0]; return 1; } return 0;

    case BI_CLZ: case BI_CLZL: case BI_CLZLL:
        if (nargs < 1 || args[0] == 0) return 0;
        v = (unsigned long)args[0];
        count = 0;
        if (id == (int)BI_CLZ) { v &= 0xFFFFFFFFUL; v <<= 32; }
        while (!(v & (1UL << 63))) { count++; v <<= 1; }
        *result = count; return 1;

    case BI_CTZ: case BI_CTZL: case BI_CTZLL:
        if (nargs < 1 || args[0] == 0) return 0;
        v = (unsigned long)args[0];
        if (id == (int)BI_CTZ) v &= 0xFFFFFFFFUL;
        count = 0;
        while (!(v & 1UL)) { count++; v >>= 1; }
        *result = count; return 1;

    case BI_POPCOUNT: case BI_POPCOUNTL: case BI_POPCOUNTLL:
        if (nargs < 1) return 0;
        v = (unsigned long)args[0];
        if (id == (int)BI_POPCOUNT) v &= 0xFFFFFFFFUL;
        count = 0;
        while (v) { count += (int)(v & 1UL); v >>= 1; }
        *result = count; return 1;

    case BI_FFS: case BI_FFSL: case BI_FFSLL:
        if (nargs < 1) return 0;
        v = (unsigned long)args[0];
        if (id == (int)BI_FFS) v &= 0xFFFFFFFFUL;
        if (v == 0) { *result = 0; }
        else { count = 1; while (!(v & 1UL)) { count++; v >>= 1; }
               *result = count; }
        return 1;

    case BI_BSWAP16:
        if (nargs < 1) return 0;
        v = (unsigned long)args[0] & 0xFFFFUL;
        *result = (long)(((v >> 8) & 0xFF) | ((v & 0xFF) << 8));
        return 1;
    case BI_BSWAP32:
        if (nargs < 1) return 0;
        v = (unsigned long)args[0] & 0xFFFFFFFFUL;
        *result = (long)(((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
                  ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000UL));
        return 1;
    case BI_BSWAP64:
        if (nargs < 1) return 0;
        v = (unsigned long)args[0];
        *result = (long)(
            ((v >> 56) & 0xFFUL) | ((v >> 40) & 0xFF00UL) |
            ((v >> 24) & 0xFF0000UL) | ((v >> 8) & 0xFF000000UL) |
            ((v << 8) & 0xFF00000000UL) |
            ((v << 24) & 0xFF0000000000UL) |
            ((v << 40) & 0xFF000000000000UL) |
            ((v << 56) & 0xFF00000000000000UL));
        return 1;

    case BI_PARITY: case BI_PARITYL: case BI_PARITYLL:
        if (nargs < 1) return 0;
        v = (unsigned long)args[0];
        if (id == (int)BI_PARITY) v &= 0xFFFFFFFFUL;
        count = 0;
        while (v) { count ^= (int)(v & 1UL); v >>= 1; }
        *result = count; return 1;

    default: return 0;
    }
}
