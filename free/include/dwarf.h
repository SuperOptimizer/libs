/*
 * dwarf.h - DWARF4 debug information constants for the free toolchain
 * Defines tags, attributes, forms, opcodes for generating .debug_* sections.
 * Pure C89. No external dependencies.
 */
#ifndef FREE_DWARF_H
#define FREE_DWARF_H

/* ---- DWARF version ---- */
#define DWARF_VERSION   4

/* ---- Unit header lengths ---- */
/* 32-bit DWARF format: 4 bytes for initial length */
#define DWARF_INITIAL_LENGTH_SIZE  4
#define DWARF_OFFSET_SIZE          4

/* ---- Tags (DW_TAG_*) ---- */
#define DW_TAG_compile_unit      0x11
#define DW_TAG_subprogram        0x2e
#define DW_TAG_variable          0x34
#define DW_TAG_base_type         0x24
#define DW_TAG_pointer_type      0x0f
#define DW_TAG_structure_type    0x13
#define DW_TAG_member            0x0d
#define DW_TAG_formal_parameter  0x05
#define DW_TAG_typedef           0x16
#define DW_TAG_array_type        0x01
#define DW_TAG_subrange_type     0x21
#define DW_TAG_union_type        0x17
#define DW_TAG_enumeration_type  0x04
#define DW_TAG_enumerator        0x28
#define DW_TAG_const_type        0x26
#define DW_TAG_volatile_type     0x35
#define DW_TAG_subroutine_type   0x15
#define DW_TAG_unspecified_type  0x3b

/* ---- Children flag ---- */
#define DW_CHILDREN_no   0
#define DW_CHILDREN_yes  1

/* ---- Attributes (DW_AT_*) ---- */
#define DW_AT_name             0x03
#define DW_AT_stmt_list        0x10
#define DW_AT_low_pc           0x11
#define DW_AT_high_pc          0x12
#define DW_AT_language         0x13
#define DW_AT_comp_dir         0x1b
#define DW_AT_producer         0x25
#define DW_AT_byte_size        0x0b
#define DW_AT_bit_size         0x0d
#define DW_AT_encoding         0x3e
#define DW_AT_type             0x49
#define DW_AT_location         0x02
#define DW_AT_data_member_location 0x38
#define DW_AT_decl_file        0x3a
#define DW_AT_decl_line        0x3b
#define DW_AT_external         0x3f
#define DW_AT_frame_base       0x40
#define DW_AT_prototyped       0x27
#define DW_AT_upper_bound      0x2f

/* ---- Forms (DW_FORM_*) ---- */
#define DW_FORM_addr           0x01
#define DW_FORM_data1          0x0b
#define DW_FORM_data2          0x05
#define DW_FORM_data4          0x06
#define DW_FORM_data8          0x07
#define DW_FORM_string         0x08
#define DW_FORM_strp           0x0e
#define DW_FORM_flag           0x0c
#define DW_FORM_flag_present   0x19
#define DW_FORM_ref4           0x13
#define DW_FORM_sec_offset     0x17
#define DW_FORM_exprloc        0x18
#define DW_FORM_udata          0x0f
#define DW_FORM_sdata          0x0d

/* ---- Base type encodings (DW_ATE_*) ---- */
#define DW_ATE_address         0x01
#define DW_ATE_boolean         0x02
#define DW_ATE_signed          0x05
#define DW_ATE_signed_char     0x06
#define DW_ATE_unsigned        0x07
#define DW_ATE_unsigned_char   0x08
#define DW_ATE_void            0x00
#define DW_ATE_float           0x04

/* ---- Location expressions (DW_OP_*) ---- */
#define DW_OP_addr             0x03
#define DW_OP_fbreg            0x91
#define DW_OP_reg0             0x50
#define DW_OP_breg0            0x70
#define DW_OP_plus_uconst      0x23
#define DW_OP_call_frame_cfa   0x9c

/* ---- Languages (DW_LANG_*) ---- */
#define DW_LANG_C89            0x0001
#define DW_LANG_C              0x0002
#define DW_LANG_C99            0x000c

/* ---- Line number standard opcodes (DW_LNS_*) ---- */
#define DW_LNS_copy            1
#define DW_LNS_advance_pc      2
#define DW_LNS_advance_line    3
#define DW_LNS_set_file        4
#define DW_LNS_set_column      5
#define DW_LNS_negate_stmt     6
#define DW_LNS_set_basic_block 7
#define DW_LNS_const_add_pc    8
#define DW_LNS_fixed_advance_pc 9
#define DW_LNS_set_prologue_end 10
#define DW_LNS_set_epilogue_begin 11
#define DW_LNS_set_isa         12

/* ---- Line number extended opcodes (DW_LNE_*) ---- */
#define DW_LNE_end_sequence    1
#define DW_LNE_set_address     2
#define DW_LNE_define_file     3

/* ---- Line number program defaults ---- */
#define DWARF_LINE_MIN_INSN_LENGTH    4  /* aarch64: all insns are 4 bytes */
#define DWARF_LINE_MAX_OPS_PER_INSN   1
#define DWARF_LINE_DEFAULT_IS_STMT    1
#define DWARF_LINE_BASE             (-5)
#define DWARF_LINE_RANGE              14
#define DWARF_LINE_OPCODE_BASE        13  /* standard opcodes 1..12 */

#endif /* FREE_DWARF_H */
