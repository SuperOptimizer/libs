/*
 * dwarf.c - DWARF4 debug information generator for the free C compiler.
 * Emits .debug_info, .debug_abbrev, .debug_line, .debug_str sections
 * as GAS-compatible assembly directives.
 * Pure C89. No external dependencies.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "free.h"
#include "dwarf.h"

/* ---- global debug flag ---- */
int cc_debug_info;

/* ---- output file (shared with gen.c) ---- */
static FILE *dw_out;

/* ---- string table for .debug_str ---- */
#define MAX_DEBUG_STRS 256
#define MAX_DEBUG_STR_LEN 256

static struct {
    char str[MAX_DEBUG_STR_LEN];
    int offset;
} debug_strs[MAX_DEBUG_STRS];
static int num_debug_strs;
static int debug_str_offset;

/* ---- abbreviation table state ---- */
static int next_abbrev_code;

/* ---- .debug_info offset tracking ---- */
static int info_offset;

/* ---- type DIE offset tracking ---- */
#define MAX_TYPE_DIES 128
struct type_die {
    int kind;          /* TY_* */
    int size;
    int is_unsigned;
    int is_ptr;        /* 1 if pointer type */
    int info_offset;   /* offset of this DIE in .debug_info */
};
static struct type_die type_dies[MAX_TYPE_DIES];
static int num_type_dies;

/* ---- line number program state ---- */
#define MAX_LINE_ENTRIES 4096

struct line_entry {
    int address_label;   /* label id for this location */
    int line;
    int file;
    int is_stmt;
};

static struct line_entry line_entries[MAX_LINE_ENTRIES];
static int num_line_entries;
static int line_label_count;

/* ---- forward declarations ---- */
static int add_debug_str(const char *s);
static void emit_dw(const char *fmt, ...);
static void emit_dw_label(const char *fmt, ...);
static void emit_dw_comment(const char *fmt, ...);

/* ---- helpers ---- */

static void emit_dw(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(dw_out, "\t");
    vfprintf(dw_out, fmt, ap);
    fprintf(dw_out, "\n");
    va_end(ap);
}

static void emit_dw_label(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(dw_out, fmt, ap);
    fprintf(dw_out, ":\n");
    va_end(ap);
}

static void emit_dw_comment(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    fprintf(dw_out, "\t/* ");
    vfprintf(dw_out, fmt, ap);
    fprintf(dw_out, " */\n");
    va_end(ap);
}

/*
 * emit_uleb128 - emit an unsigned LEB128 value as .byte directives.
 */
static void emit_uleb128(unsigned long val)
{
    if (val == 0) {
        emit_dw(".byte 0");
        return;
    }
    while (val > 0) {
        unsigned int b = (unsigned int)(val & 0x7F);
        val >>= 7;
        if (val != 0) {
            b |= 0x80;
        }
        emit_dw(".byte 0x%02x", b);
    }
}

/*
 * emit_sleb128 - emit a signed LEB128 value as .byte directives.
 */
static void emit_sleb128(long val)
{
    int more = 1;

    while (more) {
        unsigned int b = (unsigned int)(val & 0x7F);
        val >>= 7;
        if ((val == 0 && !(b & 0x40)) ||
            (val == -1 && (b & 0x40))) {
            more = 0;
        } else {
            b |= 0x80;
        }
        emit_dw(".byte 0x%02x", b);
    }
}

/*
 * uleb128_size - calculate size of an unsigned LEB128 encoding.
 */
static int uleb128_size(unsigned long val)
{
    int size = 0;

    do {
        val >>= 7;
        size++;
    } while (val != 0);
    return size;
}

/*
 * sleb128_size - calculate size of a signed LEB128 encoding.
 */
static int sleb128_size(long val)
{
    int size = 0;
    int more = 1;

    while (more) {
        unsigned int b = (unsigned int)(val & 0x7F);
        val >>= 7;
        if ((val == 0 && !(b & 0x40)) ||
            (val == -1 && (b & 0x40))) {
            more = 0;
        }
        size++;
    }
    return size;
}

/* ---- string table ---- */

/*
 * add_debug_str - add a string to .debug_str, returning its offset.
 * Deduplicates entries.
 */
static int add_debug_str(const char *s)
{
    int i;
    int len;

    for (i = 0; i < num_debug_strs; i++) {
        if (strcmp(debug_strs[i].str, s) == 0) {
            return debug_strs[i].offset;
        }
    }

    if (num_debug_strs >= MAX_DEBUG_STRS) {
        /* overflow: just use inline strings */
        return -1;
    }

    len = (int)strlen(s);
    if (len >= MAX_DEBUG_STR_LEN) {
        len = MAX_DEBUG_STR_LEN - 1;
    }

    memcpy(debug_strs[num_debug_strs].str, s, (size_t)len);
    debug_strs[num_debug_strs].str[len] = '\0';
    debug_strs[num_debug_strs].offset = debug_str_offset;
    num_debug_strs++;
    debug_str_offset += len + 1;

    return debug_strs[num_debug_strs - 1].offset;
}

/* ---- type DIE management ---- */

/*
 * find_type_die - find or create a type DIE, return its .debug_info offset.
 * Returns -1 if type cannot be represented.
 */
static int find_type_die(struct type *ty)
{
    int i;
    int kind;
    int size;
    int is_unsigned;
    int is_ptr;

    if (ty == NULL) {
        return -1;
    }

    kind = ty->kind;
    size = ty->size;
    is_unsigned = ty->is_unsigned;
    is_ptr = (kind == TY_PTR) ? 1 : 0;

    /* check if we already have this type */
    for (i = 0; i < num_type_dies; i++) {
        if (type_dies[i].kind == kind &&
            type_dies[i].size == size &&
            type_dies[i].is_unsigned == is_unsigned &&
            type_dies[i].is_ptr == is_ptr) {
            return type_dies[i].info_offset;
        }
    }

    /* will be assigned during emit */
    if (num_type_dies >= MAX_TYPE_DIES) {
        return -1;
    }

    i = num_type_dies++;
    type_dies[i].kind = kind;
    type_dies[i].size = size;
    type_dies[i].is_unsigned = is_unsigned;
    type_dies[i].is_ptr = is_ptr;
    type_dies[i].info_offset = 0; /* assigned later */

    return 0; /* placeholder - caller should use label-based refs */
}

/* ---- abbreviation table ---- */

/*
 * Abbreviation codes used in .debug_info:
 *   1 = DW_TAG_compile_unit
 *   2 = DW_TAG_subprogram
 *   3 = DW_TAG_base_type
 *   4 = DW_TAG_pointer_type
 *   5 = DW_TAG_variable (local)
 *   6 = DW_TAG_formal_parameter
 *   7 = DW_TAG_structure_type
 *   8 = DW_TAG_member
 */
#define ABBREV_COMPILE_UNIT     1
#define ABBREV_SUBPROGRAM       2
#define ABBREV_BASE_TYPE        3
#define ABBREV_POINTER_TYPE     4
#define ABBREV_VARIABLE         5
#define ABBREV_FORMAL_PARAM     6
#define ABBREV_STRUCT_TYPE      7
#define ABBREV_MEMBER           8

/*
 * dwarf_emit_abbrev - emit the .debug_abbrev section.
 */
static void dwarf_emit_abbrev(void)
{
    fprintf(dw_out, "\n\t.section .debug_abbrev,\"\",%%progbits\n");
    emit_dw_label(".Ldebug_abbrev0");

    /* Abbrev 1: DW_TAG_compile_unit */
    emit_dw_comment("abbrev %d: compile_unit", ABBREV_COMPILE_UNIT);
    emit_uleb128(ABBREV_COMPILE_UNIT);     /* abbreviation code */
    emit_uleb128(DW_TAG_compile_unit);     /* tag */
    emit_dw(".byte %d", DW_CHILDREN_yes);  /* has children */
    /* attributes: */
    emit_uleb128(DW_AT_producer); emit_uleb128(DW_FORM_strp);
    emit_uleb128(DW_AT_language); emit_uleb128(DW_FORM_data2);
    emit_uleb128(DW_AT_name);     emit_uleb128(DW_FORM_strp);
    emit_uleb128(DW_AT_comp_dir); emit_uleb128(DW_FORM_strp);
    emit_uleb128(DW_AT_low_pc);   emit_uleb128(DW_FORM_addr);
    emit_uleb128(DW_AT_high_pc);  emit_uleb128(DW_FORM_data8);
    emit_uleb128(DW_AT_stmt_list); emit_uleb128(DW_FORM_sec_offset);
    emit_uleb128(0); emit_uleb128(0);      /* end of attributes */

    /* Abbrev 2: DW_TAG_subprogram */
    emit_dw_comment("abbrev %d: subprogram", ABBREV_SUBPROGRAM);
    emit_uleb128(ABBREV_SUBPROGRAM);
    emit_uleb128(DW_TAG_subprogram);
    emit_dw(".byte %d", DW_CHILDREN_yes);
    emit_uleb128(DW_AT_name);       emit_uleb128(DW_FORM_strp);
    emit_uleb128(DW_AT_low_pc);     emit_uleb128(DW_FORM_addr);
    emit_uleb128(DW_AT_high_pc);    emit_uleb128(DW_FORM_data8);
    emit_uleb128(DW_AT_frame_base); emit_uleb128(DW_FORM_exprloc);
    emit_uleb128(DW_AT_external);   emit_uleb128(DW_FORM_flag_present);
    emit_uleb128(0); emit_uleb128(0);

    /* Abbrev 3: DW_TAG_base_type */
    emit_dw_comment("abbrev %d: base_type", ABBREV_BASE_TYPE);
    emit_uleb128(ABBREV_BASE_TYPE);
    emit_uleb128(DW_TAG_base_type);
    emit_dw(".byte %d", DW_CHILDREN_no);
    emit_uleb128(DW_AT_name);      emit_uleb128(DW_FORM_strp);
    emit_uleb128(DW_AT_encoding);  emit_uleb128(DW_FORM_data1);
    emit_uleb128(DW_AT_byte_size); emit_uleb128(DW_FORM_data1);
    emit_uleb128(0); emit_uleb128(0);

    /* Abbrev 4: DW_TAG_pointer_type */
    emit_dw_comment("abbrev %d: pointer_type", ABBREV_POINTER_TYPE);
    emit_uleb128(ABBREV_POINTER_TYPE);
    emit_uleb128(DW_TAG_pointer_type);
    emit_dw(".byte %d", DW_CHILDREN_no);
    emit_uleb128(DW_AT_byte_size); emit_uleb128(DW_FORM_data1);
    emit_uleb128(DW_AT_type);      emit_uleb128(DW_FORM_ref4);
    emit_uleb128(0); emit_uleb128(0);

    /* Abbrev 5: DW_TAG_variable */
    emit_dw_comment("abbrev %d: variable", ABBREV_VARIABLE);
    emit_uleb128(ABBREV_VARIABLE);
    emit_uleb128(DW_TAG_variable);
    emit_dw(".byte %d", DW_CHILDREN_no);
    emit_uleb128(DW_AT_name);     emit_uleb128(DW_FORM_strp);
    emit_uleb128(DW_AT_type);     emit_uleb128(DW_FORM_ref4);
    emit_uleb128(DW_AT_location); emit_uleb128(DW_FORM_exprloc);
    emit_uleb128(0); emit_uleb128(0);

    /* Abbrev 6: DW_TAG_formal_parameter */
    emit_dw_comment("abbrev %d: formal_parameter", ABBREV_FORMAL_PARAM);
    emit_uleb128(ABBREV_FORMAL_PARAM);
    emit_uleb128(DW_TAG_formal_parameter);
    emit_dw(".byte %d", DW_CHILDREN_no);
    emit_uleb128(DW_AT_name);     emit_uleb128(DW_FORM_strp);
    emit_uleb128(DW_AT_type);     emit_uleb128(DW_FORM_ref4);
    emit_uleb128(DW_AT_location); emit_uleb128(DW_FORM_exprloc);
    emit_uleb128(0); emit_uleb128(0);

    /* Abbrev 7: DW_TAG_structure_type */
    emit_dw_comment("abbrev %d: structure_type", ABBREV_STRUCT_TYPE);
    emit_uleb128(ABBREV_STRUCT_TYPE);
    emit_uleb128(DW_TAG_structure_type);
    emit_dw(".byte %d", DW_CHILDREN_yes);
    emit_uleb128(DW_AT_name);      emit_uleb128(DW_FORM_strp);
    emit_uleb128(DW_AT_byte_size); emit_uleb128(DW_FORM_data4);
    emit_uleb128(0); emit_uleb128(0);

    /* Abbrev 8: DW_TAG_member */
    emit_dw_comment("abbrev %d: member", ABBREV_MEMBER);
    emit_uleb128(ABBREV_MEMBER);
    emit_uleb128(DW_TAG_member);
    emit_dw(".byte %d", DW_CHILDREN_no);
    emit_uleb128(DW_AT_name);     emit_uleb128(DW_FORM_strp);
    emit_uleb128(DW_AT_type);     emit_uleb128(DW_FORM_ref4);
    emit_uleb128(DW_AT_data_member_location); emit_uleb128(DW_FORM_data4);
    emit_uleb128(0); emit_uleb128(0);

    /* End of abbreviation table */
    emit_dw(".byte 0");
}

/* ---- .debug_str section ---- */

static void dwarf_emit_str(void)
{
    int i;

    if (num_debug_strs == 0) {
        return;
    }

    fprintf(dw_out, "\n\t.section .debug_str,\"MS\",%%progbits,1\n");

    for (i = 0; i < num_debug_strs; i++) {
        emit_dw(".asciz \"%s\"", debug_strs[i].str);
    }
}

/* ---- base type names ---- */

static const char *base_type_name(struct type *ty)
{
    if (ty == NULL) {
        return "void";
    }

    switch (ty->kind) {
    case TY_VOID:  return "void";
    case TY_CHAR:  return ty->is_unsigned ? "unsigned char" : "char";
    case TY_SHORT: return ty->is_unsigned ? "unsigned short" : "short";
    case TY_INT:   return ty->is_unsigned ? "unsigned int" : "int";
    case TY_LONG:  return ty->is_unsigned ? "unsigned long" : "long";
    case TY_LLONG: return ty->is_unsigned ? "unsigned long long"
                                          : "long long";
    default:       return "int";
    }
}

static int base_type_encoding(struct type *ty)
{
    if (ty == NULL) {
        return DW_ATE_void;
    }

    switch (ty->kind) {
    case TY_VOID:  return DW_ATE_void;
    case TY_CHAR:
        return ty->is_unsigned ? DW_ATE_unsigned_char : DW_ATE_signed_char;
    case TY_BOOL:
        return DW_ATE_boolean;
    default:
        return ty->is_unsigned ? DW_ATE_unsigned : DW_ATE_signed;
    }
}

/* ---- .debug_info section ---- */

/*
 * dwarf_emit_info - emit .debug_info section for one compilation unit.
 * Walks the AST program and generates DIEs for functions, variables, types.
 */
static void dwarf_emit_info(struct node *prog, const char *filename,
                            const char *comp_dir)
{
    struct node *n;
    int producer_str;
    int file_str;
    int dir_str;
    int name_str;
    int type_name_str;

    /* pre-register strings */
    producer_str = add_debug_str("free-cc");
    file_str = add_debug_str(filename);
    dir_str = add_debug_str(comp_dir);

    fprintf(dw_out, "\n\t.section .debug_info,\"\",%%progbits\n");
    emit_dw_label(".Ldebug_info0");

    /* compilation unit header */
    emit_dw_comment("DWARF compilation unit header");
    emit_dw(".4byte .Ldebug_info_end0 - .Ldebug_info_start0");
    emit_dw_label(".Ldebug_info_start0");
    emit_dw(".2byte %d", DWARF_VERSION);             /* version */
    emit_dw(".4byte .Ldebug_abbrev0");                /* abbrev offset */
    emit_dw(".byte 8");                               /* address size */

    /* compile_unit DIE (abbrev 1) */
    emit_dw_comment("DW_TAG_compile_unit");
    emit_uleb128(ABBREV_COMPILE_UNIT);
    emit_dw(".4byte %d", producer_str);               /* DW_AT_producer */
    emit_dw(".2byte %d", DW_LANG_C89);                /* DW_AT_language */
    emit_dw(".4byte %d", file_str);                   /* DW_AT_name */
    emit_dw(".4byte %d", dir_str);                    /* DW_AT_comp_dir */
    emit_dw(".8byte .Ltext0");                        /* DW_AT_low_pc */
    emit_dw(".8byte .Letext0 - .Ltext0");             /* DW_AT_high_pc */
    emit_dw(".4byte .Ldebug_line0");                  /* DW_AT_stmt_list */

    /* emit base type DIEs */
    /* void (we represent as 0-byte base type) */
    emit_dw_label(".Ldie_type_void");
    emit_dw_comment("DW_TAG_base_type: void");
    type_name_str = add_debug_str("void");
    emit_uleb128(ABBREV_BASE_TYPE);
    emit_dw(".4byte %d", type_name_str);
    emit_dw(".byte %d", DW_ATE_void);
    emit_dw(".byte 0");

    /* char */
    emit_dw_label(".Ldie_type_char");
    emit_dw_comment("DW_TAG_base_type: char");
    type_name_str = add_debug_str("char");
    emit_uleb128(ABBREV_BASE_TYPE);
    emit_dw(".4byte %d", type_name_str);
    emit_dw(".byte %d", DW_ATE_signed_char);
    emit_dw(".byte 1");

    /* unsigned char */
    emit_dw_label(".Ldie_type_uchar");
    emit_dw_comment("DW_TAG_base_type: unsigned char");
    type_name_str = add_debug_str("unsigned char");
    emit_uleb128(ABBREV_BASE_TYPE);
    emit_dw(".4byte %d", type_name_str);
    emit_dw(".byte %d", DW_ATE_unsigned_char);
    emit_dw(".byte 1");

    /* short */
    emit_dw_label(".Ldie_type_short");
    emit_dw_comment("DW_TAG_base_type: short");
    type_name_str = add_debug_str("short");
    emit_uleb128(ABBREV_BASE_TYPE);
    emit_dw(".4byte %d", type_name_str);
    emit_dw(".byte %d", DW_ATE_signed);
    emit_dw(".byte 2");

    /* unsigned short */
    emit_dw_label(".Ldie_type_ushort");
    emit_dw_comment("DW_TAG_base_type: unsigned short");
    type_name_str = add_debug_str("unsigned short");
    emit_uleb128(ABBREV_BASE_TYPE);
    emit_dw(".4byte %d", type_name_str);
    emit_dw(".byte %d", DW_ATE_unsigned);
    emit_dw(".byte 2");

    /* int */
    emit_dw_label(".Ldie_type_int");
    emit_dw_comment("DW_TAG_base_type: int");
    type_name_str = add_debug_str("int");
    emit_uleb128(ABBREV_BASE_TYPE);
    emit_dw(".4byte %d", type_name_str);
    emit_dw(".byte %d", DW_ATE_signed);
    emit_dw(".byte 4");

    /* unsigned int */
    emit_dw_label(".Ldie_type_uint");
    emit_dw_comment("DW_TAG_base_type: unsigned int");
    type_name_str = add_debug_str("unsigned int");
    emit_uleb128(ABBREV_BASE_TYPE);
    emit_dw(".4byte %d", type_name_str);
    emit_dw(".byte %d", DW_ATE_unsigned);
    emit_dw(".byte 4");

    /* long */
    emit_dw_label(".Ldie_type_long");
    emit_dw_comment("DW_TAG_base_type: long");
    type_name_str = add_debug_str("long");
    emit_uleb128(ABBREV_BASE_TYPE);
    emit_dw(".4byte %d", type_name_str);
    emit_dw(".byte %d", DW_ATE_signed);
    emit_dw(".byte 8");

    /* unsigned long */
    emit_dw_label(".Ldie_type_ulong");
    emit_dw_comment("DW_TAG_base_type: unsigned long");
    type_name_str = add_debug_str("unsigned long");
    emit_uleb128(ABBREV_BASE_TYPE);
    emit_dw(".4byte %d", type_name_str);
    emit_dw(".byte %d", DW_ATE_unsigned);
    emit_dw(".byte 8");

    /* emit subprogram DIEs for each function */
    for (n = prog; n != NULL; n = n->next) {
        if (n->kind == ND_FUNCDEF && n->name != NULL) {
            struct node *param;
            struct node *body_stmt;
            name_str = add_debug_str(n->name);

            emit_dw_comment("DW_TAG_subprogram: %s", n->name);
            emit_uleb128(ABBREV_SUBPROGRAM);
            emit_dw(".4byte %d", name_str);           /* DW_AT_name */
            emit_dw(".8byte %s", n->name);             /* DW_AT_low_pc */
            emit_dw(".8byte .Lfunc_end_%s - %s",
                    n->name, n->name);                 /* DW_AT_high_pc */
            /* DW_AT_frame_base: DW_OP_call_frame_cfa */
            emit_dw(".byte 1");   /* exprloc length */
            emit_dw(".byte 0x%02x", DW_OP_call_frame_cfa);
            /* DW_AT_external: flag_present (implicit) */

            /* emit parameter DIEs */
            for (param = n->args; param != NULL; param = param->next) {
                int param_name_str;
                const char *type_label;
                int loc_size;

                if (param->name == NULL) {
                    continue;
                }
                param_name_str = add_debug_str(param->name);

                /* determine type label */
                type_label = ".Ldie_type_int";
                if (param->ty != NULL) {
                    switch (param->ty->kind) {
                    case TY_CHAR:
                        type_label = param->ty->is_unsigned
                            ? ".Ldie_type_uchar" : ".Ldie_type_char";
                        break;
                    case TY_SHORT:
                        type_label = param->ty->is_unsigned
                            ? ".Ldie_type_ushort" : ".Ldie_type_short";
                        break;
                    case TY_INT:
                        type_label = param->ty->is_unsigned
                            ? ".Ldie_type_uint" : ".Ldie_type_int";
                        break;
                    case TY_LONG:
                    case TY_LLONG:
                        type_label = param->ty->is_unsigned
                            ? ".Ldie_type_ulong" : ".Ldie_type_long";
                        break;
                    case TY_PTR:
                        type_label = ".Ldie_type_long"; /* pointer as long */
                        break;
                    default:
                        type_label = ".Ldie_type_int";
                        break;
                    }
                }

                emit_dw_comment("DW_TAG_formal_parameter: %s",
                                param->name);
                emit_uleb128(ABBREV_FORMAL_PARAM);
                emit_dw(".4byte %d", param_name_str);
                emit_dw(".4byte %s - .Ldebug_info_start0", type_label);

                /* location: DW_OP_fbreg offset */
                loc_size = sleb128_size((long)(-param->offset));
                emit_dw(".byte %d", 1 + loc_size);
                emit_dw(".byte 0x%02x", DW_OP_fbreg);
                emit_sleb128((long)(-param->offset));
            }

            /* scan block body for local variable declarations */
            if (n->body != NULL && n->body->kind == ND_BLOCK) {
                for (body_stmt = n->body->body; body_stmt != NULL;
                     body_stmt = body_stmt->next) {
                    int var_name_str;
                    const char *vtype_label;
                    int vloc_size;

                    if (body_stmt->kind != ND_VAR) {
                        continue;
                    }
                    if (body_stmt->name == NULL || body_stmt->offset <= 0) {
                        continue;
                    }

                    var_name_str = add_debug_str(body_stmt->name);

                    vtype_label = ".Ldie_type_int";
                    if (body_stmt->ty != NULL) {
                        switch (body_stmt->ty->kind) {
                        case TY_CHAR:
                            vtype_label = body_stmt->ty->is_unsigned
                                ? ".Ldie_type_uchar"
                                : ".Ldie_type_char";
                            break;
                        case TY_SHORT:
                            vtype_label = body_stmt->ty->is_unsigned
                                ? ".Ldie_type_ushort"
                                : ".Ldie_type_short";
                            break;
                        case TY_INT:
                            vtype_label = body_stmt->ty->is_unsigned
                                ? ".Ldie_type_uint"
                                : ".Ldie_type_int";
                            break;
                        case TY_LONG:
                        case TY_LLONG:
                            vtype_label = body_stmt->ty->is_unsigned
                                ? ".Ldie_type_ulong"
                                : ".Ldie_type_long";
                            break;
                        case TY_PTR:
                            vtype_label = ".Ldie_type_long";
                            break;
                        default:
                            vtype_label = ".Ldie_type_int";
                            break;
                        }
                    }

                    emit_dw_comment("DW_TAG_variable: %s",
                                    body_stmt->name);
                    emit_uleb128(ABBREV_VARIABLE);
                    emit_dw(".4byte %d", var_name_str);
                    emit_dw(".4byte %s - .Ldebug_info_start0",
                            vtype_label);

                    vloc_size = sleb128_size(
                        (long)(-body_stmt->offset));
                    emit_dw(".byte %d", 1 + vloc_size);
                    emit_dw(".byte 0x%02x", DW_OP_fbreg);
                    emit_sleb128((long)(-body_stmt->offset));
                }
            }

            /* end of subprogram children */
            emit_dw(".byte 0");
        }
    }

    /* end of compile_unit children */
    emit_dw(".byte 0");

    emit_dw_label(".Ldebug_info_end0");
}

/* ---- .debug_line section ---- */

/*
 * dwarf_emit_line - emit .debug_line section with line number program.
 * Maps instruction addresses to source file:line numbers.
 */
static void dwarf_emit_line(const char *filename)
{
    int i;
    int prev_line;

    fprintf(dw_out, "\n\t.section .debug_line,\"\",%%progbits\n");
    emit_dw_label(".Ldebug_line0");

    /* line number program header */
    emit_dw_comment("DWARF line number program header");
    emit_dw(".4byte .Ldebug_line_end0 - .Ldebug_line_start0");
    emit_dw_label(".Ldebug_line_start0");
    emit_dw(".2byte %d", DWARF_VERSION);              /* version */
    emit_dw(".4byte .Ldebug_line_hdr_end0 - .Ldebug_line_hdr_start0");
    emit_dw_label(".Ldebug_line_hdr_start0");

    /* program parameters */
    emit_dw(".byte %d", DWARF_LINE_MIN_INSN_LENGTH);
    emit_dw(".byte %d", DWARF_LINE_MAX_OPS_PER_INSN);
    emit_dw(".byte %d", DWARF_LINE_DEFAULT_IS_STMT);
    emit_dw(".byte %d", (int)(signed char)DWARF_LINE_BASE);
    emit_dw(".byte %d", DWARF_LINE_RANGE);
    emit_dw(".byte %d", DWARF_LINE_OPCODE_BASE);

    /* standard opcode lengths (opcodes 1..12) */
    emit_dw(".byte 0");  /* DW_LNS_copy: 0 args */
    emit_dw(".byte 1");  /* DW_LNS_advance_pc: 1 arg */
    emit_dw(".byte 1");  /* DW_LNS_advance_line: 1 arg */
    emit_dw(".byte 1");  /* DW_LNS_set_file: 1 arg */
    emit_dw(".byte 1");  /* DW_LNS_set_column: 1 arg */
    emit_dw(".byte 0");  /* DW_LNS_negate_stmt: 0 args */
    emit_dw(".byte 0");  /* DW_LNS_set_basic_block: 0 args */
    emit_dw(".byte 0");  /* DW_LNS_const_add_pc: 0 args */
    emit_dw(".byte 1");  /* DW_LNS_fixed_advance_pc: 1 arg */
    emit_dw(".byte 0");  /* DW_LNS_set_prologue_end: 0 args */
    emit_dw(".byte 0");  /* DW_LNS_set_epilogue_begin: 0 args */
    emit_dw(".byte 1");  /* DW_LNS_set_isa: 1 arg */

    /* include directories (empty for now, null terminated) */
    emit_dw(".byte 0");

    /* file name table */
    emit_dw(".asciz \"%s\"", filename);  /* file name */
    emit_uleb128(0);  /* directory index */
    emit_uleb128(0);  /* time of last modification */
    emit_uleb128(0);  /* length in bytes */
    emit_dw(".byte 0");  /* end of file names */

    emit_dw_label(".Ldebug_line_hdr_end0");

    /* line number program opcodes */
    emit_dw_comment("line number program");

    if (num_line_entries > 0) {
        /* set address to start of text */
        emit_dw(".byte 0");    /* extended opcode */
        emit_dw(".byte 9");    /* length (1 + 8) */
        emit_dw(".byte %d", DW_LNE_set_address);
        emit_dw(".8byte .Ldl%d", line_entries[0].address_label);

        prev_line = 1;

        for (i = 0; i < num_line_entries; i++) {
            int line_delta;

            if (i > 0) {
                /* advance address */
                emit_dw(".byte 0");
                emit_dw(".byte 9");
                emit_dw(".byte %d", DW_LNE_set_address);
                emit_dw(".8byte .Ldl%d",
                        line_entries[i].address_label);
            }

            /* advance line */
            line_delta = line_entries[i].line - prev_line;
            if (line_delta != 0) {
                emit_dw(".byte %d", DW_LNS_advance_line);
                emit_sleb128((long)line_delta);
            }

            /* copy row */
            emit_dw(".byte %d", DW_LNS_copy);

            prev_line = line_entries[i].line;
        }
    }

    /* end sequence */
    emit_dw(".byte 0");    /* extended opcode */
    emit_dw(".byte 9");    /* length */
    emit_dw(".byte %d", DW_LNE_set_address);
    emit_dw(".8byte .Letext0");
    emit_dw(".byte 0");
    emit_dw(".byte 1");
    emit_dw(".byte %d", DW_LNE_end_sequence);

    emit_dw_label(".Ldebug_line_end0");
}

/* ---- public interface ---- */

/*
 * dwarf_init - initialize DWARF generation state.
 */
void dwarf_init(void)
{
    num_debug_strs = 0;
    debug_str_offset = 0;
    next_abbrev_code = 1;
    info_offset = 0;
    num_type_dies = 0;
    num_line_entries = 0;
    line_label_count = 0;
}

/*
 * dwarf_add_line_entry - record a mapping from address to source line.
 * Called from gen.c during code generation.
 * Returns the label id that should be emitted in .text before the instruction.
 */
int dwarf_add_line_entry(int line)
{
    int label_id;

    if (!cc_debug_info || num_line_entries >= MAX_LINE_ENTRIES) {
        return -1;
    }

    /* deduplicate: skip if same line as last entry */
    if (num_line_entries > 0 &&
        line_entries[num_line_entries - 1].line == line) {
        return -1;
    }

    label_id = line_label_count++;
    line_entries[num_line_entries].address_label = label_id;
    line_entries[num_line_entries].line = line;
    line_entries[num_line_entries].file = 1;
    line_entries[num_line_entries].is_stmt = 1;
    num_line_entries++;

    return label_id;
}

/*
 * dwarf_emit_line_label - emit a .Ldl<id> label in the .text section.
 * Called from gen.c right before the instruction at the given address.
 */
void dwarf_emit_line_label(FILE *f, int label_id)
{
    if (label_id >= 0) {
        fprintf(f, ".Ldl%d:\n", label_id);
    }
}

/*
 * dwarf_generate - emit all DWARF debug sections.
 * Called after code generation is complete.
 */
void dwarf_generate(struct node *prog, FILE *outfile,
                    const char *filename, const char *comp_dir)
{
    dw_out = outfile;

    if (comp_dir == NULL) {
        comp_dir = ".";
    }

    /* emit .debug_abbrev */
    dwarf_emit_abbrev();

    /* emit .debug_info */
    dwarf_emit_info(prog, filename, comp_dir);

    /* emit .debug_line */
    dwarf_emit_line(filename);

    /* emit .debug_str */
    dwarf_emit_str();

    /* suppress unused variable warnings */
    (void)find_type_die;
    (void)base_type_name;
    (void)base_type_encoding;
    (void)uleb128_size;
}
