/*
 * encode_x86.c - x86_64 instruction encoder for the free toolchain
 * Implements all x86_* functions declared in x86_64.h
 * Variable-length encoding with REX prefixes, ModR/M, SIB.
 * Pure C89. No external dependencies.
 */

#include "x86_64.h"

/* ---- REX prefix helpers ---- */

u8 x86_rex(int w, int reg, int rm)
{
    u8 r;

    r = REX_BASE;
    if (w) {
        r |= REX_W;
    }
    if (reg >= 8) {
        r |= REX_R;
    }
    if (rm >= 8) {
        r |= REX_B;
    }
    return r;
}

u8 x86_rex_sib(int w, int reg, int idx, int base)
{
    u8 r;

    r = REX_BASE;
    if (w) {
        r |= REX_W;
    }
    if (reg >= 8) {
        r |= REX_R;
    }
    if (idx >= 8) {
        r |= REX_X;
    }
    if (base >= 8) {
        r |= REX_B;
    }
    return r;
}

/* ---- Internal helpers ---- */

/*
 * need_rex - return 1 if a REX prefix is required.
 * REX is needed for 64-bit ops (W=1) or extended registers (>=8).
 */
static int need_rex(int w, int reg, int rm)
{
    return w || reg >= 8 || rm >= 8;
}

/*
 * emit_disp_modrm - emit ModR/M byte with displacement for [base+disp].
 * Handles the RBP/R13 base special case (needs explicit disp8 of 0).
 * Handles the RSP/R12 base special case (needs SIB byte).
 * Returns number of bytes written.
 */
static int emit_disp_modrm(u8 *buf, int reg, int base, i32 disp)
{
    int n;
    int b3;

    n = 0;
    b3 = base & 7;

    if (disp == 0 && b3 != 5) {
        /* mod=00: no displacement (except rbp/r13 which always need disp) */
        if (b3 == 4) {
            /* rsp/r12: need SIB byte */
            buf[n++] = MODRM(0, reg, 4);
            buf[n++] = 0x24; /* SIB: scale=0, index=rsp(none), base=rsp */
        } else {
            buf[n++] = MODRM(0, reg, base);
        }
    } else if (disp >= -128 && disp <= 127) {
        /* mod=01: disp8 */
        if (b3 == 4) {
            buf[n++] = MODRM(1, reg, 4);
            buf[n++] = 0x24;
        } else {
            buf[n++] = MODRM(1, reg, base);
        }
        buf[n++] = (u8)(disp & 0xFF);
    } else {
        /* mod=10: disp32 */
        if (b3 == 4) {
            buf[n++] = MODRM(2, reg, 4);
            buf[n++] = 0x24;
        } else {
            buf[n++] = MODRM(2, reg, base);
        }
        buf[n++] = (u8)(disp & 0xFF);
        buf[n++] = (u8)((disp >> 8) & 0xFF);
        buf[n++] = (u8)((disp >> 16) & 0xFF);
        buf[n++] = (u8)((disp >> 24) & 0xFF);
    }
    return n;
}

/*
 * write_imm32 - write a 32-bit immediate in little-endian.
 */
static void write_imm32(u8 *buf, i32 val)
{
    buf[0] = (u8)(val & 0xFF);
    buf[1] = (u8)((val >> 8) & 0xFF);
    buf[2] = (u8)((val >> 16) & 0xFF);
    buf[3] = (u8)((val >> 24) & 0xFF);
}

/* ---- Move instructions ---- */

/* MOV r64, r64: REX.W 89 /r */
int x86_mov_rr(u8 *buf, int dst, int src)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, src, dst);
    buf[n++] = 0x89;
    buf[n++] = MODRM(3, src, dst);
    return n;
}

/* MOV r64, imm64 (or shorter forms) */
int x86_mov_ri(u8 *buf, int dst, long imm)
{
    int n;

    n = 0;

    if (imm == 0) {
        /* XOR r32, r32 (2 or 3 bytes, clears full 64-bit) */
        return x86_xor_rr32(buf, dst, dst);
    }

    if (imm > 0 && imm <= 0x7FFFFFFFL) {
        /* MOV r32, imm32 (zero-extends to 64-bit) */
        return x86_mov_ri32(buf, dst, (i32)imm);
    }

    if (imm >= -0x80000000L && imm < 0) {
        /* MOV r64, imm32 (sign-extended): REX.W C7 /0 id */
        buf[n++] = x86_rex(1, 0, dst);
        buf[n++] = 0xC7;
        buf[n++] = MODRM(3, 0, dst);
        write_imm32(buf + n, (i32)imm);
        n += 4;
        return n;
    }

    /* MOV r64, imm64: REX.W B8+rd io */
    buf[n++] = x86_rex(1, 0, dst);
    buf[n++] = (u8)(0xB8 + (dst & 7));
    buf[n++] = (u8)(imm & 0xFF);
    buf[n++] = (u8)((imm >> 8) & 0xFF);
    buf[n++] = (u8)((imm >> 16) & 0xFF);
    buf[n++] = (u8)((imm >> 24) & 0xFF);
    buf[n++] = (u8)((imm >> 32) & 0xFF);
    buf[n++] = (u8)((imm >> 40) & 0xFF);
    buf[n++] = (u8)((imm >> 48) & 0xFF);
    buf[n++] = (u8)((imm >> 56) & 0xFF);
    return n;
}

/* MOV r32, imm32: opcode B8+rd, no REX.W (zero-extends to 64-bit) */
int x86_mov_ri32(u8 *buf, int dst, i32 imm)
{
    int n;

    n = 0;
    if (dst >= 8) {
        buf[n++] = REX_BASE | REX_B;
    }
    buf[n++] = (u8)(0xB8 + (dst & 7));
    write_imm32(buf + n, imm);
    n += 4;
    return n;
}

/* MOV r64, [base + disp32]: REX.W 8B /r */
int x86_mov_rm(u8 *buf, int dst, int base, i32 disp)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, dst, base);
    buf[n++] = 0x8B;
    n += emit_disp_modrm(buf + n, dst, base, disp);
    return n;
}

/* MOV [base + disp32], r64: REX.W 89 /r */
int x86_mov_mr(u8 *buf, int base, i32 disp, int src)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, src, base);
    buf[n++] = 0x89;
    n += emit_disp_modrm(buf + n, src, base, disp);
    return n;
}

/* MOVZX r64, byte [base + disp]: REX.W 0F B6 /r */
int x86_movzx_rm8(u8 *buf, int dst, int base, i32 disp)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, dst, base);
    buf[n++] = 0x0F;
    buf[n++] = 0xB6;
    n += emit_disp_modrm(buf + n, dst, base, disp);
    return n;
}

/* MOVSX r64, byte [base + disp]: REX.W 0F BE /r */
int x86_movsx_rm8(u8 *buf, int dst, int base, i32 disp)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, dst, base);
    buf[n++] = 0x0F;
    buf[n++] = 0xBE;
    n += emit_disp_modrm(buf + n, dst, base, disp);
    return n;
}

/* MOV byte [base + disp], r8: 88 /r (with REX if needed) */
int x86_mov_mr8(u8 *buf, int base, i32 disp, int src)
{
    int n;

    n = 0;
    /* Always emit REX if using sil, dil, bpl, spl or extended regs */
    if (need_rex(0, src, base) || src >= 4) {
        buf[n++] = x86_rex(0, src, base);
    }
    buf[n++] = 0x88;
    n += emit_disp_modrm(buf + n, src, base, disp);
    return n;
}

/* MOV r32, [base + disp32]: 8B /r (no REX.W) */
int x86_mov_rm32(u8 *buf, int dst, int base, i32 disp)
{
    int n;

    n = 0;
    if (need_rex(0, dst, base)) {
        buf[n++] = x86_rex(0, dst, base);
    }
    buf[n++] = 0x8B;
    n += emit_disp_modrm(buf + n, dst, base, disp);
    return n;
}

/* MOV [base + disp32], r32: 89 /r (no REX.W) */
int x86_mov_mr32(u8 *buf, int base, i32 disp, int src)
{
    int n;

    n = 0;
    if (need_rex(0, src, base)) {
        buf[n++] = x86_rex(0, src, base);
    }
    buf[n++] = 0x89;
    n += emit_disp_modrm(buf + n, src, base, disp);
    return n;
}

/* MOVSXD r64, [base + disp32]: REX.W 63 /r */
int x86_movsxd_rm(u8 *buf, int dst, int base, i32 disp)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, dst, base);
    buf[n++] = 0x63;
    n += emit_disp_modrm(buf + n, dst, base, disp);
    return n;
}

/* ---- Push/Pop ---- */

/* PUSH r64: 50+rd (REX if extended) */
int x86_push_r(u8 *buf, int reg)
{
    int n;

    n = 0;
    if (reg >= 8) {
        buf[n++] = REX_BASE | REX_B;
    }
    buf[n++] = (u8)(0x50 + (reg & 7));
    return n;
}

/* POP r64: 58+rd (REX if extended) */
int x86_pop_r(u8 *buf, int reg)
{
    int n;

    n = 0;
    if (reg >= 8) {
        buf[n++] = REX_BASE | REX_B;
    }
    buf[n++] = (u8)(0x58 + (reg & 7));
    return n;
}

/* ---- Arithmetic ---- */

/* ADD r64, r64: REX.W 01 /r */
int x86_add_rr(u8 *buf, int dst, int src)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, src, dst);
    buf[n++] = 0x01;
    buf[n++] = MODRM(3, src, dst);
    return n;
}

/* ADD r64, imm32: REX.W 81 /0 id (or 83 /0 ib for imm8) */
int x86_add_ri(u8 *buf, int dst, i32 imm)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, 0, dst);
    if (imm >= -128 && imm <= 127) {
        buf[n++] = 0x83;
        buf[n++] = MODRM(3, 0, dst);
        buf[n++] = (u8)(imm & 0xFF);
    } else {
        buf[n++] = 0x81;
        buf[n++] = MODRM(3, 0, dst);
        write_imm32(buf + n, imm);
        n += 4;
    }
    return n;
}

/* SUB r64, r64: REX.W 29 /r */
int x86_sub_rr(u8 *buf, int dst, int src)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, src, dst);
    buf[n++] = 0x29;
    buf[n++] = MODRM(3, src, dst);
    return n;
}

/* SUB r64, imm32: REX.W 81 /5 id (or 83 /5 ib for imm8) */
int x86_sub_ri(u8 *buf, int dst, i32 imm)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, 0, dst);
    if (imm >= -128 && imm <= 127) {
        buf[n++] = 0x83;
        buf[n++] = MODRM(3, 5, dst);
        buf[n++] = (u8)(imm & 0xFF);
    } else {
        buf[n++] = 0x81;
        buf[n++] = MODRM(3, 5, dst);
        write_imm32(buf + n, imm);
        n += 4;
    }
    return n;
}

/* IMUL r64, r64: REX.W 0F AF /r */
int x86_imul_rr(u8 *buf, int dst, int src)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, dst, src);
    buf[n++] = 0x0F;
    buf[n++] = 0xAF;
    buf[n++] = MODRM(3, dst, src);
    return n;
}

/* CQO: REX.W 99 */
int x86_cqo(u8 *buf)
{
    buf[0] = 0x48; /* REX.W */
    buf[1] = 0x99;
    return 2;
}

/* IDIV r64: REX.W F7 /7 */
int x86_idiv_r(u8 *buf, int src)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, 0, src);
    buf[n++] = 0xF7;
    buf[n++] = MODRM(3, 7, src);
    return n;
}

/* DIV r64: REX.W F7 /6 */
int x86_div_r(u8 *buf, int src)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, 0, src);
    buf[n++] = 0xF7;
    buf[n++] = MODRM(3, 6, src);
    return n;
}

/* NEG r64: REX.W F7 /3 */
int x86_neg_r(u8 *buf, int reg)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, 0, reg);
    buf[n++] = 0xF7;
    buf[n++] = MODRM(3, 3, reg);
    return n;
}

/* NOT r64: REX.W F7 /2 */
int x86_not_r(u8 *buf, int reg)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, 0, reg);
    buf[n++] = 0xF7;
    buf[n++] = MODRM(3, 2, reg);
    return n;
}

/* ---- Logical ---- */

/* AND r64, r64: REX.W 21 /r */
int x86_and_rr(u8 *buf, int dst, int src)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, src, dst);
    buf[n++] = 0x21;
    buf[n++] = MODRM(3, src, dst);
    return n;
}

/* OR r64, r64: REX.W 09 /r */
int x86_or_rr(u8 *buf, int dst, int src)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, src, dst);
    buf[n++] = 0x09;
    buf[n++] = MODRM(3, src, dst);
    return n;
}

/* XOR r64, r64: REX.W 31 /r */
int x86_xor_rr(u8 *buf, int dst, int src)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, src, dst);
    buf[n++] = 0x31;
    buf[n++] = MODRM(3, src, dst);
    return n;
}

/* XOR r32, r32: 31 /r (no REX.W, for zeroing) */
int x86_xor_rr32(u8 *buf, int dst, int src)
{
    int n;

    n = 0;
    if (need_rex(0, src, dst)) {
        buf[n++] = x86_rex(0, src, dst);
    }
    buf[n++] = 0x31;
    buf[n++] = MODRM(3, src, dst);
    return n;
}

/* AND r64, imm32: REX.W 81 /4 id (or 83 /4 ib for imm8) */
int x86_and_ri(u8 *buf, int dst, i32 imm)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, 0, dst);
    if (imm >= -128 && imm <= 127) {
        buf[n++] = 0x83;
        buf[n++] = MODRM(3, 4, dst);
        buf[n++] = (u8)(imm & 0xFF);
    } else {
        buf[n++] = 0x81;
        buf[n++] = MODRM(3, 4, dst);
        write_imm32(buf + n, imm);
        n += 4;
    }
    return n;
}

/* ---- Shifts ---- */

/* SHL r64, cl: REX.W D3 /4 */
int x86_shl_cl(u8 *buf, int dst)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, 0, dst);
    buf[n++] = 0xD3;
    buf[n++] = MODRM(3, 4, dst);
    return n;
}

/* SHR r64, cl: REX.W D3 /5 */
int x86_shr_cl(u8 *buf, int dst)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, 0, dst);
    buf[n++] = 0xD3;
    buf[n++] = MODRM(3, 5, dst);
    return n;
}

/* SAR r64, cl: REX.W D3 /7 */
int x86_sar_cl(u8 *buf, int dst)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, 0, dst);
    buf[n++] = 0xD3;
    buf[n++] = MODRM(3, 7, dst);
    return n;
}

/* ---- Compare ---- */

/* CMP r64, r64: REX.W 39 /r */
int x86_cmp_rr(u8 *buf, int a, int b)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, b, a);
    buf[n++] = 0x39;
    buf[n++] = MODRM(3, b, a);
    return n;
}

/* CMP r64, imm32: REX.W 81 /7 id (or 83 /7 ib for imm8) */
int x86_cmp_ri(u8 *buf, int reg, i32 imm)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, 0, reg);
    if (imm >= -128 && imm <= 127) {
        buf[n++] = 0x83;
        buf[n++] = MODRM(3, 7, reg);
        buf[n++] = (u8)(imm & 0xFF);
    } else {
        buf[n++] = 0x81;
        buf[n++] = MODRM(3, 7, reg);
        write_imm32(buf + n, imm);
        n += 4;
    }
    return n;
}

/* TEST r64, r64: REX.W 85 /r */
int x86_test_rr(u8 *buf, int a, int b)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, b, a);
    buf[n++] = 0x85;
    buf[n++] = MODRM(3, b, a);
    return n;
}

/* SETcc r8: 0F 90+cc /0 (with REX if needed for sil, etc.) */
int x86_setcc(u8 *buf, int cc, int dst)
{
    int n;

    n = 0;
    /* Need REX prefix for spl/bpl/sil/dil (regs 4-7) or extended */
    if (dst >= 4) {
        buf[n++] = x86_rex(0, 0, dst);
    }
    buf[n++] = 0x0F;
    buf[n++] = (u8)(0x90 + (cc & 0xF));
    buf[n++] = MODRM(3, 0, dst);
    return n;
}

/* MOVZX r64, r8: REX.W 0F B6 /r */
int x86_movzx_rr8(u8 *buf, int dst, int src)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, dst, src);
    buf[n++] = 0x0F;
    buf[n++] = 0xB6;
    buf[n++] = MODRM(3, dst, src);
    return n;
}

/* MOVSX r64, r8: REX.W 0F BE /r */
int x86_movsx_rr8(u8 *buf, int dst, int src)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, dst, src);
    buf[n++] = 0x0F;
    buf[n++] = 0xBE;
    buf[n++] = MODRM(3, dst, src);
    return n;
}

/* MOVSX r64, r16: REX.W 0F BF /r */
int x86_movsx_rr16(u8 *buf, int dst, int src)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, dst, src);
    buf[n++] = 0x0F;
    buf[n++] = 0xBF;
    buf[n++] = MODRM(3, dst, src);
    return n;
}

/* MOVSXD r64, r32: REX.W 63 /r */
int x86_movsxd_rr(u8 *buf, int dst, int src)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, dst, src);
    buf[n++] = 0x63;
    buf[n++] = MODRM(3, dst, src);
    return n;
}

/* MOV r32, r32: 89 /r (no REX.W, zero-extends) */
int x86_mov_rr32(u8 *buf, int dst, int src)
{
    int n;

    n = 0;
    if (need_rex(0, src, dst)) {
        buf[n++] = x86_rex(0, src, dst);
    }
    buf[n++] = 0x89;
    buf[n++] = MODRM(3, src, dst);
    return n;
}

/* ---- Branch ---- */

/* JMP rel32: E9 cd */
int x86_jmp_rel32(u8 *buf, i32 offset)
{
    buf[0] = 0xE9;
    write_imm32(buf + 1, offset);
    return 5;
}

/* Jcc rel32: 0F 80+cc cd */
int x86_jcc_rel32(u8 *buf, int cc, i32 offset)
{
    buf[0] = 0x0F;
    buf[1] = (u8)(0x80 + (cc & 0xF));
    write_imm32(buf + 2, offset);
    return 6;
}

/* CALL rel32: E8 cd */
int x86_call_rel32(u8 *buf, i32 offset)
{
    buf[0] = 0xE8;
    write_imm32(buf + 1, offset);
    return 5;
}

/* CALL r64: FF /2 (with REX if extended) */
int x86_call_r(u8 *buf, int reg)
{
    int n;

    n = 0;
    if (reg >= 8) {
        buf[n++] = REX_BASE | REX_B;
    }
    buf[n++] = 0xFF;
    buf[n++] = MODRM(3, 2, reg);
    return n;
}

/* RET: C3 */
int x86_ret(u8 *buf)
{
    buf[0] = 0xC3;
    return 1;
}

/* ---- LEA ---- */

/* LEA r64, [base + disp32]: REX.W 8D /r */
int x86_lea(u8 *buf, int dst, int base, i32 disp)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, dst, base);
    buf[n++] = 0x8D;
    n += emit_disp_modrm(buf + n, dst, base, disp);
    return n;
}

/* LEA r64, [rip + disp32]: REX.W 8D /r mod=00 rm=5 */
int x86_lea_rip(u8 *buf, int dst, i32 disp)
{
    int n;

    n = 0;
    buf[n++] = x86_rex(1, dst, 0);
    buf[n++] = 0x8D;
    buf[n++] = MODRM(0, dst, 5); /* mod=00, rm=5 => [rip+disp32] */
    write_imm32(buf + n, disp);
    n += 4;
    return n;
}

/* ---- Misc ---- */

/* NOP: 90 */
int x86_nop(u8 *buf)
{
    buf[0] = 0x90;
    return 1;
}

/* SYSCALL: 0F 05 */
int x86_syscall(u8 *buf)
{
    buf[0] = 0x0F;
    buf[1] = 0x05;
    return 2;
}
