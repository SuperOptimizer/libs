/*
 * ir_print.c - Pretty-print SSA intermediate representation.
 * Outputs human-readable IR in an LLVM-inspired text format.
 * Pure C89. All variables declared at top of block.
 */

#include <stdio.h>
#include <string.h>
#include "free.h"
#include "ir.h"

/* ---- type name strings ---- */

static const char *irt_name(enum ir_type_kind t)
{
    switch (t) {
    case IRT_VOID:  return "void";
    case IRT_I1:    return "i1";
    case IRT_I8:    return "i8";
    case IRT_I16:   return "i16";
    case IRT_I32:   return "i32";
    case IRT_I64:   return "i64";
    case IRT_PTR:   return "ptr";
    default:        return "?";
    }
}

/* ---- opcode name strings ---- */

static const char *op_name(enum ir_op op)
{
    switch (op) {
    case IR_CONST:      return "const";
    case IR_GLOBAL_ADDR: return "global_addr";
    case IR_ADD:        return "add";
    case IR_SUB:        return "sub";
    case IR_MUL:        return "mul";
    case IR_DIV:        return "div";
    case IR_MOD:        return "mod";
    case IR_NEG:        return "neg";
    case IR_AND:        return "and";
    case IR_OR:         return "or";
    case IR_XOR:        return "xor";
    case IR_NOT:        return "not";
    case IR_SHL:        return "shl";
    case IR_SHR:        return "shr";
    case IR_SAR:        return "sar";
    case IR_EQ:         return "eq";
    case IR_NE:         return "ne";
    case IR_LT:         return "lt";
    case IR_LE:         return "le";
    case IR_GT:         return "gt";
    case IR_GE:         return "ge";
    case IR_ULT:        return "ult";
    case IR_ULE:        return "ule";
    case IR_UGT:        return "ugt";
    case IR_UGE:        return "uge";
    case IR_LOAD:       return "load";
    case IR_STORE:      return "store";
    case IR_ALLOCA:     return "alloca";
    case IR_SEXT:       return "sext";
    case IR_ZEXT:       return "zext";
    case IR_TRUNC:      return "trunc";
    case IR_BITCAST:    return "bitcast";
    case IR_PTRTOINT:   return "ptrtoint";
    case IR_INTTOPTR:   return "inttoptr";
    case IR_BR:         return "br";
    case IR_BR_COND:    return "br_cond";
    case IR_RET:        return "ret";
    case IR_CALL:       return "call";
    case IR_PHI:        return "phi";
    default:            return "???";
    }
}

/* ---- value printing ---- */

static void print_val(FILE *out, struct ir_val *v)
{
    if (v == NULL) {
        fprintf(out, "null");
        return;
    }
    fprintf(out, "%%%d", v->id);
}

static void print_typed_val(FILE *out, struct ir_val *v)
{
    if (v == NULL) {
        fprintf(out, "void null");
        return;
    }
    fprintf(out, "%s ", irt_name(v->type));
    print_val(out, v);
}

/* ---- block label printing ---- */

static void print_block_ref(FILE *out, struct ir_block *bb)
{
    if (bb == NULL) {
        fprintf(out, "bb?");
        return;
    }
    if (bb->label != NULL) {
        fprintf(out, "bb%d", bb->id);
    } else {
        fprintf(out, "bb%d", bb->id);
    }
}

/* ---- instruction printing ---- */

static void print_inst(FILE *out, struct ir_inst *inst)
{
    int i;

    fprintf(out, "    ");

    switch (inst->op) {
    case IR_CONST:
        if (inst->result != NULL) {
            print_val(out, inst->result);
            fprintf(out, " = const %s %ld\n",
                    irt_name(inst->result->type), inst->imm);
        }
        return;

    case IR_GLOBAL_ADDR:
        if (inst->result != NULL) {
            print_val(out, inst->result);
            fprintf(out, " = global_addr @%s\n",
                    inst->name ? inst->name : "?");
        }
        return;

    case IR_ALLOCA:
        if (inst->result != NULL) {
            print_val(out, inst->result);
            fprintf(out, " = alloca %ld\n", inst->imm);
        }
        return;

    case IR_LOAD:
        if (inst->result != NULL) {
            print_val(out, inst->result);
            fprintf(out, " = load %s, ",
                    irt_name(inst->result->type));
            if (inst->nargs > 0) {
                print_typed_val(out, inst->args[0]);
            }
            fprintf(out, "\n");
        }
        return;

    case IR_STORE:
        fprintf(out, "store ");
        if (inst->nargs >= 2) {
            print_typed_val(out, inst->args[0]);
            fprintf(out, ", ");
            print_typed_val(out, inst->args[1]);
        }
        fprintf(out, "\n");
        return;

    case IR_BR:
        fprintf(out, "br ");
        print_block_ref(out, inst->target);
        fprintf(out, "\n");
        return;

    case IR_BR_COND:
        fprintf(out, "br_cond ");
        if (inst->nargs > 0) {
            print_typed_val(out, inst->args[0]);
        }
        fprintf(out, ", ");
        print_block_ref(out, inst->true_bb);
        fprintf(out, ", ");
        print_block_ref(out, inst->false_bb);
        fprintf(out, "\n");
        return;

    case IR_RET:
        fprintf(out, "ret");
        if (inst->nargs > 0 && inst->args[0] != NULL) {
            fprintf(out, " ");
            print_typed_val(out, inst->args[0]);
        } else {
            fprintf(out, " void");
        }
        fprintf(out, "\n");
        return;

    case IR_CALL:
        if (inst->result != NULL) {
            print_val(out, inst->result);
            fprintf(out, " = ");
        }
        fprintf(out, "call %s @%s(",
                inst->result ? irt_name(inst->result->type) : "void",
                inst->name ? inst->name : "?");
        for (i = 0; i < inst->nargs; i++) {
            if (i > 0) {
                fprintf(out, ", ");
            }
            print_typed_val(out, inst->args[i]);
        }
        fprintf(out, ")\n");
        return;

    case IR_PHI:
        if (inst->result != NULL) {
            print_val(out, inst->result);
            fprintf(out, " = phi %s ",
                    irt_name(inst->result->type));
        }
        for (i = 0; i < inst->nargs; i++) {
            if (i > 0) {
                fprintf(out, ", ");
            }
            fprintf(out, "[ ");
            print_val(out, inst->args[i]);
            fprintf(out, ", ");
            if (inst->phi_blocks != NULL && inst->phi_blocks[i] != NULL) {
                print_block_ref(out, inst->phi_blocks[i]);
            } else {
                fprintf(out, "bb?");
            }
            fprintf(out, " ]");
        }
        fprintf(out, "\n");
        return;

    /* unary operations */
    case IR_NEG:
    case IR_NOT:
    case IR_SEXT:
    case IR_ZEXT:
    case IR_TRUNC:
    case IR_BITCAST:
    case IR_PTRTOINT:
    case IR_INTTOPTR:
        if (inst->result != NULL) {
            print_val(out, inst->result);
            fprintf(out, " = %s %s ",
                    op_name(inst->op),
                    irt_name(inst->result->type));
            if (inst->nargs > 0) {
                print_val(out, inst->args[0]);
            }
            fprintf(out, "\n");
        }
        return;

    /* binary operations (arithmetic, bitwise, comparison) */
    default:
        break;
    }

    /* generic binary instruction */
    if (inst->result != NULL) {
        print_val(out, inst->result);
        fprintf(out, " = %s %s ",
                op_name(inst->op),
                irt_name(inst->result->type));
        if (inst->nargs >= 2) {
            print_val(out, inst->args[0]);
            fprintf(out, ", ");
            print_val(out, inst->args[1]);
        } else if (inst->nargs == 1) {
            print_val(out, inst->args[0]);
        }
        fprintf(out, "\n");
    } else {
        fprintf(out, "%s", op_name(inst->op));
        for (i = 0; i < inst->nargs; i++) {
            if (i > 0) {
                fprintf(out, ",");
            }
            fprintf(out, " ");
            print_typed_val(out, inst->args[i]);
        }
        fprintf(out, "\n");
    }
}

/* ---- block printing ---- */

static void print_block(FILE *out, struct ir_block *bb)
{
    struct ir_inst *inst;
    int i;

    /* block header */
    if (bb->label != NULL) {
        fprintf(out, "bb%d", bb->id);
        fprintf(out, ":  /* %s */", bb->label);
    } else {
        fprintf(out, "bb%d:", bb->id);
    }

    /* predecessor list */
    if (bb->npreds > 0) {
        fprintf(out, "  /* preds: ");
        for (i = 0; i < bb->npreds; i++) {
            if (i > 0) {
                fprintf(out, ", ");
            }
            print_block_ref(out, bb->preds[i]);
        }
        fprintf(out, " */");
    }
    fprintf(out, "\n");

    /* instructions */
    for (inst = bb->first; inst != NULL; inst = inst->next) {
        print_inst(out, inst);
    }
}

/* ---- function printing ---- */

static void print_func(FILE *out, struct ir_func *func)
{
    struct ir_block *bb;
    int i;

    fprintf(out, "func @%s(", func->name ? func->name : "?");
    for (i = 0; i < func->nparams; i++) {
        if (i > 0) {
            fprintf(out, ", ");
        }
        if (func->params != NULL && func->params[i] != NULL) {
            print_typed_val(out, func->params[i]);
        }
    }
    fprintf(out, ") -> %s {\n", irt_name(func->ret_type));

    for (bb = func->blocks; bb != NULL; bb = bb->next) {
        print_block(out, bb);
    }

    fprintf(out, "}\n");
}

/* ---- global printing ---- */

static void print_global(FILE *out, struct ir_global *g)
{
    fprintf(out, "global @%s : %s, size %d, align %d",
            g->name ? g->name : "?",
            irt_name(g->type),
            g->size, g->align);
    if (g->init_val != 0) {
        fprintf(out, " = %ld", g->init_val);
    }
    fprintf(out, "\n");
}

/* ---- public interface ---- */

void ir_print(struct ir_module *mod, FILE *out)
{
    struct ir_global *g;
    struct ir_func *f;

    if (mod == NULL) {
        return;
    }

    fprintf(out, "/* free-cc SSA IR */\n\n");

    /* globals */
    for (g = mod->globals; g != NULL; g = g->next) {
        print_global(out, g);
    }
    if (mod->globals != NULL) {
        fprintf(out, "\n");
    }

    /* functions */
    for (f = mod->funcs; f != NULL; f = f->next) {
        print_func(out, f);
        fprintf(out, "\n");
    }
}
