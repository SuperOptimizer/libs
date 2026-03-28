/*
 * ir.h - SSA intermediate representation for the free C compiler.
 * Three-address code in basic-block form with phi functions.
 * Pure C89. No external dependencies.
 */
#ifndef FREE_SSA_IR_H
#define FREE_SSA_IR_H

#include "free.h"
#include <stdio.h>

/* ---- IR opcodes ---- */
enum ir_op {
    /* Constants */
    IR_CONST,           /* result = immediate value */
    IR_GLOBAL_ADDR,     /* result = address of global symbol */

    /* Arithmetic */
    IR_ADD, IR_SUB, IR_MUL, IR_DIV, IR_MOD,
    IR_NEG,

    /* Bitwise */
    IR_AND, IR_OR, IR_XOR, IR_NOT,
    IR_SHL, IR_SHR, IR_SAR, /* logical vs arithmetic shift right */

    /* Comparison (produces i1) */
    IR_EQ, IR_NE, IR_LT, IR_LE, IR_GT, IR_GE,
    IR_ULT, IR_ULE, IR_UGT, IR_UGE, /* unsigned */

    /* Memory */
    IR_LOAD,            /* result = *ptr */
    IR_STORE,           /* *ptr = value (no result) */
    IR_ALLOCA,          /* result = stack slot address */

    /* Conversions */
    IR_SEXT, IR_ZEXT, IR_TRUNC,
    IR_BITCAST, IR_PTRTOINT, IR_INTTOPTR,

    /* Control flow (terminators) */
    IR_BR,              /* unconditional branch */
    IR_BR_COND,         /* conditional branch */
    IR_RET,             /* return */
    IR_CALL,            /* result = call(func, args...) */

    /* SSA */
    IR_PHI              /* phi(val_from_bb1, val_from_bb2, ...) */
};

/* ---- IR types ---- */
enum ir_type_kind {
    IRT_VOID,
    IRT_I1,     /* boolean */
    IRT_I8,     /* char */
    IRT_I16,    /* short */
    IRT_I32,    /* int */
    IRT_I64,    /* long, pointer */
    IRT_PTR     /* typed pointer */
};

/* ---- IR value (SSA name) ---- */
struct ir_val {
    int id;                 /* unique SSA name: %0, %1, %2, ... */
    enum ir_type_kind type;
    struct ir_inst *def;    /* instruction that defines this value */
};

/* ---- IR instruction ---- */
struct ir_inst {
    enum ir_op op;
    struct ir_val *result;  /* NULL for void ops (store, br, ret void) */
    struct ir_val **args;   /* operands */
    int nargs;
    long imm;               /* immediate for IR_CONST, size for IR_ALLOCA */
    char *name;             /* symbol name for IR_GLOBAL_ADDR, IR_CALL */
    struct ir_block *parent;
    struct ir_inst *next;   /* linked list within block */
    struct ir_inst *prev;
    /* For IR_PHI: parallel arrays of (value, source_block) */
    struct ir_block **phi_blocks;
    /* For IR_BR_COND: true_target, false_target */
    struct ir_block *true_bb;
    struct ir_block *false_bb;
    /* For IR_BR: target */
    struct ir_block *target;
};

/* ---- basic block ---- */
struct ir_block {
    int id;                 /* block number: bb0, bb1, ... */
    char *label;            /* optional label name */
    struct ir_inst *first;  /* first instruction */
    struct ir_inst *last;   /* last instruction (terminator) */
    struct ir_block **preds; /* predecessor blocks */
    int npreds;
    int preds_cap;
    struct ir_block **succs; /* successor blocks */
    int nsuccs;
    struct ir_block *next;  /* linked list in function */
    /* Dominator tree (for future optimization passes) */
    struct ir_block *idom;
    struct ir_block **dom_frontier;
    int ndom_frontier;
};

/* ---- global variable ---- */
struct ir_global {
    char *name;
    enum ir_type_kind type;
    long init_val;          /* initial value (0 = bss) */
    int size;               /* size in bytes */
    int align;              /* alignment in bytes */
    struct ir_global *next;
};

/* ---- function ---- */
struct ir_func {
    char *name;
    struct ir_val **params;
    int nparams;
    enum ir_type_kind ret_type;
    unsigned int attr_flags; /* function attributes from the parser */
    int is_static;          /* 1 if declared with 'static' storage class */
    struct ir_block *entry;  /* entry block */
    struct ir_block *blocks; /* linked list of all blocks */
    int nblocks;
    int next_val_id;         /* counter for SSA names */
    int next_block_id;
    struct ir_func *next;    /* linked list of functions */
};

/* ---- module (translation unit) ---- */
struct ir_module {
    struct ir_func *funcs;
    struct ir_global *globals;
    struct arena *arena;
};

/* ---- public interface ---- */

/* ir.c - build IR from AST */
struct ir_module *ir_build(struct node *ast, struct arena *a);

/* ir_print.c - pretty-print IR */
void ir_print(struct ir_module *mod, FILE *out);

/* ir_serialize.c - serialize/deserialize IR for LTO */
int ir_serialize(struct ir_module *mod, unsigned char **buf, int *len);
struct ir_module *ir_deserialize(unsigned char *buf, int len,
                                 struct arena *a);
int ir_write_to_elf(const char *obj_path, struct ir_module *mod);
struct ir_module *ir_read_from_elf(const char *obj_path, struct arena *a);

/* opt_mem2reg.c - Promote memory to registers (SSA construction) */
void opt_mem2reg(struct ir_func *f);

/* opt_sccp.c - Sparse Conditional Constant Propagation */
void opt_sccp(struct ir_func *func, struct arena *arena);

/* opt_dce.c - Dead Code Elimination */
void opt_dce(struct ir_func *func);

/* opt_inline.c - Function inlining */
void opt_inline(struct ir_module *mod, struct arena *arena);

/* lto.c - link-time optimization */
struct ir_module *lto_merge(struct ir_module **modules, int nmodules,
                            struct arena *a);
void lto_optimize(struct ir_module *merged);

#endif /* FREE_SSA_IR_H */
