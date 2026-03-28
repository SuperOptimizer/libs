/*
 * pic.c - Position-independent code generation for the free C compiler.
 * When -fPIC is enabled, globals are accessed via GOT and external
 * functions are called via PLT stubs.
 * Pure C89.
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "free.h"

/* global PIC flag, set by cc.c when -fPIC is passed */
int cc_pic_enabled;

/*
 * pic_emit_global_addr - emit PIC code to load a global symbol address
 * into x0. Uses ADRP + LDR from GOT instead of ADRP + ADD.
 */
void pic_emit_global_addr(FILE *out, const char *name)
{
    fprintf(out, "\tadrp x0, :got:%s\n", name);
    fprintf(out, "\tldr x0, [x0, :got_lo12:%s]\n", name);
}

/*
 * pic_emit_string_addr - emit PIC code to load a string literal address.
 * String literals are in .rodata within the same DSO, so they use
 * normal ADRP + ADD (no GOT indirection needed).
 */
void pic_emit_string_addr(FILE *out, int label_id)
{
    fprintf(out, "\tadrp x0, .LS%d\n", label_id);
    fprintf(out, "\tadd x0, x0, :lo12:.LS%d\n", label_id);
}

/*
 * pic_emit_call - emit a function call. In PIC mode, uses BL with
 * the PLT stub (the assembler/linker handles the @PLT suffix).
 */
void pic_emit_call(FILE *out, const char *name)
{
    fprintf(out, "\tbl %s\n", name);
}
