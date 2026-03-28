/*
 * encode.c - AArch64 instruction encoder for the free toolchain
 * Implements all a64_* functions declared in aarch64.h
 * Pure C89. No external dependencies.
 */

#include "aarch64.h"

/* ---- Wide immediate moves ---- */

/* MOVZ: sf=1, opc=10, 1_10_100101_hw_imm16_Rd */
u32 a64_movz(int rd, u16 imm, int shift)
{
    u32 hw = (u32)(shift / 16) & 3;
    return (u32)0xD2800000
         | (hw << 21)
         | ((u32)imm << 5)
         | ((u32)rd & 0x1F);
}

/* MOVK: sf=1, opc=11, 1_11_100101_hw_imm16_Rd */
u32 a64_movk(int rd, u16 imm, int shift)
{
    u32 hw = (u32)(shift / 16) & 3;
    return (u32)0xF2800000
         | (hw << 21)
         | ((u32)imm << 5)
         | ((u32)rd & 0x1F);
}

/* MOVN: sf=1, opc=00, 1_00_100101_hw_imm16_Rd */
u32 a64_movn(int rd, u16 imm, int shift)
{
    u32 hw = (u32)(shift / 16) & 3;
    return (u32)0x92800000
         | (hw << 21)
         | ((u32)imm << 5)
         | ((u32)rd & 0x1F);
}

/* MOVZ 32-bit: sf=0, 0_10_100101_hw_imm16_Rd */
u32 a64_movz_w(int rd, u16 imm, int shift)
{
    u32 hw = (u32)(shift / 16) & 3;
    return (u32)0x52800000
         | (hw << 21)
         | ((u32)imm << 5)
         | ((u32)rd & 0x1F);
}

/* MOVK 32-bit: sf=0, 0_11_100101_hw_imm16_Rd */
u32 a64_movk_w(int rd, u16 imm, int shift)
{
    u32 hw = (u32)(shift / 16) & 3;
    return (u32)0x72800000
         | (hw << 21)
         | ((u32)imm << 5)
         | ((u32)rd & 0x1F);
}

/* MOVN 32-bit: sf=0, 0_00_100101_hw_imm16_Rd */
u32 a64_movn_w(int rd, u16 imm, int shift)
{
    u32 hw = (u32)(shift / 16) & 3;
    return (u32)0x12800000
         | (hw << 21)
         | ((u32)imm << 5)
         | ((u32)rd & 0x1F);
}

/* ---- Arithmetic - register ---- */

/* ADD reg: 1_0_0_01011_00_0_Rm_000000_Rn_Rd */
u32 a64_add_r(int rd, int rn, int rm)
{
    return (u32)0x8B000000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* SUB reg: 1_1_0_01011_00_0_Rm_000000_Rn_Rd */
u32 a64_sub_r(int rd, int rn, int rm)
{
    return (u32)0xCB000000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* SUBS reg: 1_1_1_01011_00_0_Rm_000000_Rn_Rd */
u32 a64_subs_r(int rd, int rn, int rm)
{
    return (u32)0xEB000000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* MUL: MADD with Ra=XZR: 1_00_11011_000_Rm_0_11111_Rn_Rd */
u32 a64_mul(int rd, int rn, int rm)
{
    return (u32)0x9B007C00
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* SDIV: 1_00_11010110_Rm_00001_1_Rn_Rd */
u32 a64_sdiv(int rd, int rn, int rm)
{
    return (u32)0x9AC00C00
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* UDIV: 1_00_11010110_Rm_00001_0_Rn_Rd */
u32 a64_udiv(int rd, int rn, int rm)
{
    return (u32)0x9AC00800
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* MSUB: 1_00_11011_000_Rm_1_Ra_Rn_Rd */
u32 a64_msub(int rd, int rn, int rm, int ra)
{
    return (u32)0x9B008000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)ra & 0x1F) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* ---- Arithmetic - immediate ---- */

/* ADD imm: 1_0_0_100010_0_imm12_Rn_Rd */
u32 a64_add_i(int rd, int rn, u32 imm12)
{
    return (u32)0x91000000
         | ((imm12 & 0xFFF) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* SUB imm: 1_1_0_100010_0_imm12_Rn_Rd */
u32 a64_sub_i(int rd, int rn, u32 imm12)
{
    return (u32)0xD1000000
         | ((imm12 & 0xFFF) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* SUBS imm: 1_1_1_100010_0_imm12_Rn_Rd */
u32 a64_subs_i(int rd, int rn, u32 imm12)
{
    return (u32)0xF1000000
         | ((imm12 & 0xFFF) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* ---- Logical - register ---- */

/* AND: 1_00_01010_00_0_Rm_000000_Rn_Rd */
u32 a64_and_r(int rd, int rn, int rm)
{
    return (u32)0x8A000000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* ORR: 1_01_01010_00_0_Rm_000000_Rn_Rd */
u32 a64_orr_r(int rd, int rn, int rm)
{
    return (u32)0xAA000000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* EOR: 1_10_01010_00_0_Rm_000000_Rn_Rd */
u32 a64_eor_r(int rd, int rn, int rm)
{
    return (u32)0xCA000000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* ---- Shift - register ---- */

/*
 * LSL (register) = LSLV: 1_0_0_11010110_Rm_0010_00_Rn_Rd
 * LSR (register) = LSRV: 1_0_0_11010110_Rm_0010_01_Rn_Rd
 * ASR (register) = ASRV: 1_0_0_11010110_Rm_0010_10_Rn_Rd
 */

u32 a64_lsl(int rd, int rn, int rm)
{
    return (u32)0x9AC02000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

u32 a64_lsr(int rd, int rn, int rm)
{
    return (u32)0x9AC02400
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

u32 a64_asr(int rd, int rn, int rm)
{
    return (u32)0x9AC02800
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* ---- Conditional set ---- */

/*
 * CSET Xd, cond  =>  CSINC Xd, XZR, XZR, invert(cond)
 * Encoding: 1_0_0_11010100_11111_cond^1_0_1_11111_Rd
 * = 1_00_11010100_Rm(11111)_cond_01_Rn(11111)_Rd
 * 0x9A9F07E0 is the base with Rm=XZR=31, Rn=XZR=31, o2=1
 */
u32 a64_cset(int rd, int cond)
{
    u32 inv = (u32)(cond ^ 1);
    return (u32)0x9A9F07E0
         | (inv << 12)
         | ((u32)rd & 0x1F);
}

/* ---- Branch ---- */

/* B: 0_00101_imm26 */
u32 a64_b(i32 offset)
{
    return (u32)0x14000000
         | ((u32)offset & 0x03FFFFFF);
}

/* BL: 1_00101_imm26 */
u32 a64_bl(i32 offset)
{
    return (u32)0x94000000
         | ((u32)offset & 0x03FFFFFF);
}

/* B.cond: 01010100_imm19_0_cond */
u32 a64_b_cond(int cond, i32 offset)
{
    return (u32)0x54000000
         | (((u32)offset & 0x7FFFF) << 5)
         | ((u32)cond & 0xF);
}

/* BR: 1101011_0_0_00_11111_0000_0_0_Rn_00000 */
u32 a64_br(int rn)
{
    return (u32)0xD61F0000
         | (((u32)rn & 0x1F) << 5);
}

/* BLR: 1101011_0_0_01_11111_0000_0_0_Rn_00000 */
u32 a64_blr(int rn)
{
    return (u32)0xD63F0000
         | (((u32)rn & 0x1F) << 5);
}

/* RET: BLR variant with Rn=X30 */
u32 a64_ret(void)
{
    return (u32)0xD65F03C0;
}

/* ---- Load/Store (unsigned offset, with LDUR/STUR fallback) ---- */

/*
 * LDR (64-bit unsigned offset): 11_111_0_01_01_imm12_Rn_Rt
 * LDUR (64-bit unscaled):       11_111000_01_0_imm9_00_Rn_Rt
 * offset is in bytes; if negative or unaligned, uses LDUR encoding.
 */
u32 a64_ldr(int rt, int rn, i32 offset)
{
    if (offset < 0 || (offset & 7) != 0) {
        u32 imm9 = (u32)offset & 0x1FF;
        return (u32)0xF8400000
             | (imm9 << 12)
             | (((u32)rn & 0x1F) << 5)
             | ((u32)rt & 0x1F);
    } else {
        u32 uoff = (u32)(offset / 8) & 0xFFF;
        return (u32)0xF9400000
             | (uoff << 10)
             | (((u32)rn & 0x1F) << 5)
             | ((u32)rt & 0x1F);
    }
}

/*
 * STR (64-bit unsigned offset): 11_111_0_01_00_imm12_Rn_Rt
 * STUR (64-bit unscaled):       11_111000_00_0_imm9_00_Rn_Rt
 * offset is in bytes; if negative or unaligned, uses STUR encoding.
 */
u32 a64_str(int rt, int rn, i32 offset)
{
    if (offset < 0 || (offset & 7) != 0) {
        u32 imm9 = (u32)offset & 0x1FF;
        return (u32)0xF8000000
             | (imm9 << 12)
             | (((u32)rn & 0x1F) << 5)
             | ((u32)rt & 0x1F);
    } else {
        u32 uoff = (u32)(offset / 8) & 0xFFF;
        return (u32)0xF9000000
             | (uoff << 10)
             | (((u32)rn & 0x1F) << 5)
             | ((u32)rt & 0x1F);
    }
}

/*
 * LDRB (unsigned offset): 00_111_0_01_01_imm12_Rn_Rt
 * LDURB (unscaled):       00_111000_01_0_imm9_00_Rn_Rt
 */
u32 a64_ldrb(int rt, int rn, i32 offset)
{
    if (offset < 0) {
        u32 imm9 = (u32)offset & 0x1FF;
        return (u32)0x38400000
             | (imm9 << 12)
             | (((u32)rn & 0x1F) << 5)
             | ((u32)rt & 0x1F);
    } else {
        u32 uoff = (u32)offset & 0xFFF;
        return (u32)0x39400000
             | (uoff << 10)
             | (((u32)rn & 0x1F) << 5)
             | ((u32)rt & 0x1F);
    }
}

/*
 * STRB (unsigned offset): 00_111_0_01_00_imm12_Rn_Rt
 * STURB (unscaled):       00_111000_00_0_imm9_00_Rn_Rt
 */
u32 a64_strb(int rt, int rn, i32 offset)
{
    if (offset < 0) {
        u32 imm9 = (u32)offset & 0x1FF;
        return (u32)0x38000000
             | (imm9 << 12)
             | (((u32)rn & 0x1F) << 5)
             | ((u32)rt & 0x1F);
    } else {
        u32 uoff = (u32)offset & 0xFFF;
        return (u32)0x39000000
             | (uoff << 10)
             | (((u32)rn & 0x1F) << 5)
             | ((u32)rt & 0x1F);
    }
}

/*
 * LDRH (unsigned offset): 01_111_0_01_01_imm12_Rn_Rt, scaled by 2
 * LDURH (unscaled):       01_111000_01_0_imm9_00_Rn_Rt
 */
u32 a64_ldrh(int rt, int rn, i32 offset)
{
    if (offset < 0 || (offset & 1) != 0) {
        u32 imm9 = (u32)offset & 0x1FF;
        return (u32)0x78400000
             | (imm9 << 12)
             | (((u32)rn & 0x1F) << 5)
             | ((u32)rt & 0x1F);
    } else {
        u32 uoff = (u32)(offset / 2) & 0xFFF;
        return (u32)0x79400000
             | (uoff << 10)
             | (((u32)rn & 0x1F) << 5)
             | ((u32)rt & 0x1F);
    }
}

/*
 * STRH (unsigned offset): 01_111_0_01_00_imm12_Rn_Rt, scaled by 2
 * STURH (unscaled):       01_111000_00_0_imm9_00_Rn_Rt
 */
u32 a64_strh(int rt, int rn, i32 offset)
{
    if (offset < 0 || (offset & 1) != 0) {
        u32 imm9 = (u32)offset & 0x1FF;
        return (u32)0x78000000
             | (imm9 << 12)
             | (((u32)rn & 0x1F) << 5)
             | ((u32)rt & 0x1F);
    } else {
        u32 uoff = (u32)(offset / 2) & 0xFFF;
        return (u32)0x79000000
             | (uoff << 10)
             | (((u32)rn & 0x1F) << 5)
             | ((u32)rt & 0x1F);
    }
}

/*
 * LDRSW (unsigned offset): 10_111_0_01_10_imm12_Rn_Rt, scaled by 4
 * LDURSW (unscaled):       10_111000_10_0_imm9_00_Rn_Rt
 */
u32 a64_ldrsw(int rt, int rn, i32 offset)
{
    if (offset < 0 || (offset & 3) != 0) {
        u32 imm9 = (u32)offset & 0x1FF;
        return (u32)0xB8800000
             | (imm9 << 12)
             | (((u32)rn & 0x1F) << 5)
             | ((u32)rt & 0x1F);
    } else {
        u32 uoff = (u32)(offset / 4) & 0xFFF;
        return (u32)0xB9800000
             | (uoff << 10)
             | (((u32)rn & 0x1F) << 5)
             | ((u32)rt & 0x1F);
    }
}

/*
 * LDR Wt (unsigned offset): 10_111_0_01_01_imm12_Rn_Rt, scaled by 4
 * LDUR Wt (unscaled):       10_111000_01_0_imm9_00_Rn_Rt
 */
u32 a64_ldr_w(int rt, int rn, i32 offset)
{
    if (offset < 0 || (offset & 3) != 0) {
        u32 imm9 = (u32)offset & 0x1FF;
        return (u32)0xB8400000
             | (imm9 << 12)
             | (((u32)rn & 0x1F) << 5)
             | ((u32)rt & 0x1F);
    } else {
        u32 uoff = (u32)(offset / 4) & 0xFFF;
        return (u32)0xB9400000
             | (uoff << 10)
             | (((u32)rn & 0x1F) << 5)
             | ((u32)rt & 0x1F);
    }
}

/*
 * STR Wt (unsigned offset): 10_111_0_01_00_imm12_Rn_Rt, scaled by 4
 * STUR Wt (unscaled):       10_111000_00_0_imm9_00_Rn_Rt
 */
u32 a64_str_w(int rt, int rn, i32 offset)
{
    if (offset < 0 || (offset & 3) != 0) {
        u32 imm9 = (u32)offset & 0x1FF;
        return (u32)0xB8000000
             | (imm9 << 12)
             | (((u32)rn & 0x1F) << 5)
             | ((u32)rt & 0x1F);
    } else {
        u32 uoff = (u32)(offset / 4) & 0xFFF;
        return (u32)0xB9000000
             | (uoff << 10)
             | (((u32)rn & 0x1F) << 5)
             | ((u32)rt & 0x1F);
    }
}

/* ---- Load/Store pair ---- */

/*
 * STP pre-index (64-bit): 10_101_0_011_imm7_Rt2_Rn_Rt1
 * imm7 is offset/8 (signed, 7-bit)
 */
u32 a64_stp_pre(int rt1, int rt2, int rn, i32 offset)
{
    u32 imm7 = (u32)(offset / 8) & 0x7F;
    return (u32)0xA9800000
         | (imm7 << 15)
         | (((u32)rt2 & 0x1F) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt1 & 0x1F);
}

/*
 * LDP post-index (64-bit): 10_101_0_001_imm7_Rt2_Rn_Rt1
 */
u32 a64_ldp_post(int rt1, int rt2, int rn, i32 offset)
{
    u32 imm7 = (u32)(offset / 8) & 0x7F;
    return (u32)0xA8C00000
         | (imm7 << 15)
         | (((u32)rt2 & 0x1F) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt1 & 0x1F);
}

/*
 * STP signed offset (64-bit): 10_101_0_010_imm7_Rt2_Rn_Rt1
 */
u32 a64_stp(int rt1, int rt2, int rn, i32 offset)
{
    u32 imm7 = (u32)(offset / 8) & 0x7F;
    return (u32)0xA9000000
         | (imm7 << 15)
         | (((u32)rt2 & 0x1F) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt1 & 0x1F);
}

/*
 * LDP signed offset (64-bit): 10_101_0_010_imm7_Rt2_Rn_Rt1
 * bit 22 = 1 for load
 */
u32 a64_ldp(int rt1, int rt2, int rn, i32 offset)
{
    u32 imm7 = (u32)(offset / 8) & 0x7F;
    return (u32)0xA9400000
         | (imm7 << 15)
         | (((u32)rt2 & 0x1F) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt1 & 0x1F);
}

/*
 * STP post-index (64-bit): 10_101_0_001_imm7_Rt2_Rn_Rt1
 * bit 22 = 0 for store
 */
u32 a64_stp_post(int rt1, int rt2, int rn, i32 offset)
{
    u32 imm7 = (u32)(offset / 8) & 0x7F;
    return (u32)0xA8800000
         | (imm7 << 15)
         | (((u32)rt2 & 0x1F) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt1 & 0x1F);
}

/*
 * LDP pre-index (64-bit): 10_101_0_011_imm7_Rt2_Rn_Rt1
 * bit 22 = 1 for load
 */
u32 a64_ldp_pre(int rt1, int rt2, int rn, i32 offset)
{
    u32 imm7 = (u32)(offset / 8) & 0x7F;
    return (u32)0xA9C00000
         | (imm7 << 15)
         | (((u32)rt2 & 0x1F) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt1 & 0x1F);
}

/* ---- PC-relative address ---- */

/*
 * ADRP: 1_immlo(2)_10000_immhi(19)_Rd
 * offset is in units of 4KB pages
 */
u32 a64_adrp(int rd, i32 offset)
{
    u32 immlo = (u32)offset & 0x3;
    u32 immhi = ((u32)offset >> 2) & 0x7FFFF;
    return (u32)0x90000000
         | (immlo << 29)
         | (immhi << 5)
         | ((u32)rd & 0x1F);
}

/*
 * ADR: 0_immlo(2)_10000_immhi(19)_Rd
 * offset is in bytes
 */
u32 a64_adr(int rd, i32 offset)
{
    u32 immlo = (u32)offset & 0x3;
    u32 immhi = ((u32)offset >> 2) & 0x7FFFF;
    return (u32)0x10000000
         | (immlo << 29)
         | (immhi << 5)
         | ((u32)rd & 0x1F);
}

/* ---- MVN (bitwise NOT) ---- */

/*
 * MVN Xd, Xm  =>  ORN Xd, XZR, Xm
 * 1_01_01010_00_1_Rm_000000_11111_Rd
 */
u32 a64_mvn(int rd, int rm)
{
    return (u32)0xAA200000
         | (((u32)rm & 0x1F) << 16)
         | ((u32)0x1F << 5)
         | ((u32)rd & 0x1F);
}

/* ---- Sign/Zero extend ---- */

/*
 * SXTB Xd, Wn  =>  SBFM Xd, Xn, #0, #7
 * 1_00_100110_1_000000_000111_Rn_Rd
 */
u32 a64_sxtb(int rd, int rn)
{
    return (u32)0x93401C00
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/*
 * SXTH Xd, Wn  =>  SBFM Xd, Xn, #0, #15
 * 1_00_100110_1_000000_001111_Rn_Rd
 */
u32 a64_sxth(int rd, int rn)
{
    return (u32)0x93403C00
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/*
 * SXTW Xd, Wn  =>  SBFM Xd, Xn, #0, #31
 * 1_00_100110_1_000000_011111_Rn_Rd
 */
u32 a64_sxtw(int rd, int rn)
{
    return (u32)0x93407C00
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/*
 * UXTB Wd, Wn  =>  UBFM Wd, Wn, #0, #7 (32-bit)
 * 0_10_100110_0_000000_000111_Rn_Rd
 */
u32 a64_uxtb(int rd, int rn)
{
    return (u32)0x53001C00
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/*
 * UXTH Wd, Wn  =>  UBFM Wd, Wn, #0, #15 (32-bit)
 * 0_10_100110_0_000000_001111_Rn_Rd
 */
u32 a64_uxth(int rd, int rn)
{
    return (u32)0x53003C00
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* ---- Floating-point instructions ---- */

/*
 * FP data processing (2 source): type=01 (double), type=00 (single)
 * Double: 0_0_0_11110_01_1_Rm_opcode_10_Rn_Rd
 * Single: 0_0_0_11110_00_1_Rm_opcode_10_Rn_Rd
 * opcode: 0010=FADD, 0011=FSUB, 0000=FMUL, 0001=FDIV
 */

/* FADD Dd, Dn, Dm: 0001_1110_011_Rm_001010_Rn_Rd */
u32 a64_fadd_d(int rd, int rn, int rm)
{
    return (u32)0x1E602800
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* FSUB Dd, Dn, Dm: 0001_1110_011_Rm_001110_Rn_Rd */
u32 a64_fsub_d(int rd, int rn, int rm)
{
    return (u32)0x1E603800
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* FMUL Dd, Dn, Dm: 0001_1110_011_Rm_000010_Rn_Rd */
u32 a64_fmul_d(int rd, int rn, int rm)
{
    return (u32)0x1E600800
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* FDIV Dd, Dn, Dm: 0001_1110_011_Rm_000110_Rn_Rd */
u32 a64_fdiv_d(int rd, int rn, int rm)
{
    return (u32)0x1E601800
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* FNEG Dd, Dn: 0001_1110_011_00000_010000_Rn_Rd */
u32 a64_fneg_d(int rd, int rn)
{
    return (u32)0x1E614000
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* FADD Sd, Sn, Sm: 0001_1110_001_Rm_001010_Rn_Rd */
u32 a64_fadd_s(int rd, int rn, int rm)
{
    return (u32)0x1E202800
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* FSUB Sd, Sn, Sm: 0001_1110_001_Rm_001110_Rn_Rd */
u32 a64_fsub_s(int rd, int rn, int rm)
{
    return (u32)0x1E203800
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* FMUL Sd, Sn, Sm: 0001_1110_001_Rm_000010_Rn_Rd */
u32 a64_fmul_s(int rd, int rn, int rm)
{
    return (u32)0x1E200800
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* FDIV Sd, Sn, Sm: 0001_1110_001_Rm_000110_Rn_Rd */
u32 a64_fdiv_s(int rd, int rn, int rm)
{
    return (u32)0x1E201800
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* FNEG Sd, Sn: 0001_1110_001_00000_010000_Rn_Rd */
u32 a64_fneg_s(int rd, int rn)
{
    return (u32)0x1E214000
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/*
 * FCMP Dn, Dm: 00011110_01_1_Rm_001000_Rn_00000
 * FCMP Sn, Sm: 00011110_00_1_Rm_001000_Rn_00000
 */
u32 a64_fcmp_d(int rn, int rm)
{
    return (u32)0x1E602000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5);
}

u32 a64_fcmp_s(int rn, int rm)
{
    return (u32)0x1E202000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5);
}

/*
 * FMOV Dd, Dn: 00011110_01_1_00000_010000_Rn_Rd
 * FMOV Sd, Sn: 00011110_00_1_00000_010000_Rn_Rd
 */
u32 a64_fmov_d(int rd, int rn)
{
    return (u32)0x1E604000
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

u32 a64_fmov_s(int rd, int rn)
{
    return (u32)0x1E204000
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/*
 * FMOV Dd, Xn (GPR->FPR, 64-bit):
 * 1_00_11110_01_1_00_111_000000_Rn_Rd = 0x9E670000
 */
u32 a64_fmov_d_x(int rd, int rn)
{
    return (u32)0x9E670000
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/*
 * FMOV Xd, Dn (FPR->GPR, 64-bit):
 * 1_00_11110_01_1_00_110_000000_Rn_Rd = 0x9E660000
 */
u32 a64_fmov_x_d(int rd, int rn)
{
    return (u32)0x9E660000
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/*
 * FCVT Dd, Sn (single->double): 00011110_00_1_0001_01_10000_Rn_Rd = 0x1E22C000
 */
u32 a64_fcvt_ds(int rd, int rn)
{
    return (u32)0x1E22C000
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/*
 * FCVT Sd, Dn (double->single): 00011110_01_1_0000_01_10000_Rn_Rd = 0x1E624000
 */
u32 a64_fcvt_sd(int rd, int rn)
{
    return (u32)0x1E624000
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/*
 * SCVTF Dd, Xn (signed 64-bit int -> double):
 * 1_00_11110_01_1_00_010_000000_Rn_Rd = 0x9E620000
 */
u32 a64_scvtf_d(int rd, int rn)
{
    return (u32)0x9E620000
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/*
 * SCVTF Sd, Wn (signed 32-bit int -> float):
 * 0_00_11110_00_1_00_010_000000_Rn_Rd = 0x1E220000
 */
u32 a64_scvtf_s(int rd, int rn)
{
    return (u32)0x1E220000
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/*
 * UCVTF Dd, Xn (unsigned 64-bit int -> double):
 * 1_00_11110_01_1_00_011_000000_Rn_Rd = 0x9E630000
 */
u32 a64_ucvtf_d(int rd, int rn)
{
    return (u32)0x9E630000
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/*
 * UCVTF Sd, Wn (unsigned 32-bit int -> float):
 * 0_00_11110_00_1_00_011_000000_Rn_Rd = 0x1E230000
 */
u32 a64_ucvtf_s(int rd, int rn)
{
    return (u32)0x1E230000
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/*
 * FCVTZS Xd, Dn (double -> signed 64-bit int, round toward zero):
 * 1_00_11110_01_1_11_000_000000_Rn_Rd = 0x9E780000
 */
u32 a64_fcvtzs_xd(int rd, int rn)
{
    return (u32)0x9E780000
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/*
 * FCVTZS Wd, Sn (float -> signed 32-bit int, round toward zero):
 * 0_00_11110_00_1_11_000_000000_Rn_Rd = 0x1E380000
 */
u32 a64_fcvtzs_ws(int rd, int rn)
{
    return (u32)0x1E380000
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/*
 * LDR Dt, [Xn, #off]: 11_111_1_01_01_imm12_Rn_Rt  (scaled by 8)
 */
u32 a64_ldr_d(int rt, int rn, i32 offset)
{
    u32 uoff = (u32)(offset / 8) & 0xFFF;
    return (u32)0xFD400000
         | (uoff << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/*
 * STR Dt, [Xn, #off]: 11_111_1_01_00_imm12_Rn_Rt  (scaled by 8)
 */
u32 a64_str_d(int rt, int rn, i32 offset)
{
    u32 uoff = (u32)(offset / 8) & 0xFFF;
    return (u32)0xFD000000
         | (uoff << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/*
 * LDR St, [Xn, #off]: 10_111_1_01_01_imm12_Rn_Rt  (scaled by 4)
 */
u32 a64_ldr_s(int rt, int rn, i32 offset)
{
    u32 uoff = (u32)(offset / 4) & 0xFFF;
    return (u32)0xBD400000
         | (uoff << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/*
 * STR St, [Xn, #off]: 10_111_1_01_00_imm12_Rn_Rt  (scaled by 4)
 */
u32 a64_str_s(int rt, int rn, i32 offset)
{
    u32 uoff = (u32)(offset / 4) & 0xFFF;
    return (u32)0xBD000000
         | (uoff << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* ---- Atomic/Exclusive instructions ---- */

/* LDXR Xt, [Xn]: 11_001000_0_1_0_11111_0_11111_Rn_Rt (64-bit) */
u32 a64_ldxr(int rt, int rn)
{
    return (u32)0xC85F7C00
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* LDXR Wt, [Xn]: 10_001000_0_1_0_11111_0_11111_Rn_Rt (32-bit) */
u32 a64_ldxr_w(int rt, int rn)
{
    return (u32)0x885F7C00
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* STXR Ws, Xt, [Xn]: 11_001000_0_0_0_Rs_0_11111_Rn_Rt (64-bit) */
u32 a64_stxr(int rs, int rt, int rn)
{
    return (u32)0xC8007C00
         | (((u32)rs & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* STXR Ws, Wt, [Xn]: 10_001000_0_0_0_Rs_0_11111_Rn_Rt (32-bit) */
u32 a64_stxr_w(int rs, int rt, int rn)
{
    return (u32)0x88007C00
         | (((u32)rs & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* LDAXR Xt, [Xn]: 11_001000_0_1_0_11111_1_11111_Rn_Rt (64-bit) */
u32 a64_ldaxr(int rt, int rn)
{
    return (u32)0xC85FFC00
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* STLXR Ws, Xt, [Xn]: 11_001000_0_0_0_Rs_1_11111_Rn_Rt (64-bit) */
u32 a64_stlxr(int rs, int rt, int rn)
{
    return (u32)0xC800FC00
         | (((u32)rs & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* LDADD Xs, Xt, [Xn]: 11_111000_1_0_1_Rs_0000_00_Rn_Rt */
u32 a64_ldadd(int rs, int rt, int rn)
{
    return (u32)0xF8200000
         | (((u32)rs & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* STADD Xs, [Xn]: LDADD with Rt=XZR, alias */
u32 a64_stadd(int rs, int rn)
{
    return a64_ldadd(rs, REG_XZR, rn);
}

/* SWP Xs, Xt, [Xn]: 11_111000_1_0_1_Rs_1000_00_Rn_Rt */
u32 a64_swp(int rs, int rt, int rn)
{
    return (u32)0xF8208000
         | (((u32)rs & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* CAS Xs, Xt, [Xn]: 11_001000_1_0_1_Rs_0_11111_Rn_Rt */
u32 a64_cas(int rs, int rt, int rn)
{
    return (u32)0xC8A07C00
         | (((u32)rs & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* ---- Barrier instructions ---- */

/* DMB option: 1101_0101_0000_0011_0011_CRm_1_01_11111 */
u32 a64_dmb(int option)
{
    return (u32)0xD50330BF
         | (((u32)option & 0xF) << 8);
}

/* DSB option: 1101_0101_0000_0011_0011_CRm_1_00_11111 */
u32 a64_dsb(int option)
{
    return (u32)0xD503309F
         | (((u32)option & 0xF) << 8);
}

/* ISB: 1101_0101_0000_0011_0011_0110_1_10_11111 = 0xD5033FDF */
u32 a64_isb(void)
{
    return (u32)0xD5033FDF;
}

/* ---- System register instructions ---- */

/* MRS Xt, sysreg: 1101_0101_0011_o0_op1_CRn_CRm_op2_Rt
 * sysreg is 16-bit: o0(2):op1(3):CRn(4):CRm(4):op2(3), placed at bits [20:5] */
u32 a64_mrs(int rt, u32 sysreg)
{
    return (u32)0xD5300000
         | ((sysreg & 0xFFFF) << 5)
         | ((u32)rt & 0x1F);
}

/* MSR sysreg, Xt: 1101_0101_0001_o0_op1_CRn_CRm_op2_Rt */
u32 a64_msr(u32 sysreg, int rt)
{
    return (u32)0xD5100000
         | ((sysreg & 0xFFFF) << 5)
         | ((u32)rt & 0x1F);
}

/*
 * MSR pstate, #imm4 (MSR immediate form)
 * Encoding: 1101_0101_0000_0_op1_0100_CRm_op2_11111
 * field: 0=DAIFSet(op1=3,op2=6), 1=DAIFClr(op1=3,op2=7)
 *        2=SPSel(op1=0,op2=5)
 * CRm = imm4
 */
u32 a64_msr_imm(int field, int imm4)
{
    u32 op1, op2;
    switch (field) {
        case 0: op1 = 3; op2 = 6; break; /* DAIFSet */
        case 1: op1 = 3; op2 = 7; break; /* DAIFClr */
        case 2: op1 = 0; op2 = 5; break; /* SPSel */
        default: op1 = 3; op2 = 6; break;
    }
    return (u32)0xD5000000
         | (op1 << 16)
         | ((u32)0x4 << 12)
         | (((u32)imm4 & 0xF) << 8)
         | (op2 << 5)
         | 0x1F;
}

/* ---- Exception/System instructions ---- */

/* ERET: 1101_0110_1001_1111_0000_0011_1110_0000 */
u32 a64_eret(void)
{
    return (u32)0xD69F03E0;
}

/* HVC #imm16: 1101_0100_000_imm16_000_10 */
u32 a64_hvc(u32 imm16)
{
    return (u32)0xD4000002
         | ((imm16 & 0xFFFF) << 5);
}

/* SMC #imm16: 1101_0100_000_imm16_000_11 */
u32 a64_smc(u32 imm16)
{
    return (u32)0xD4000003
         | ((imm16 & 0xFFFF) << 5);
}

/* WFE: 1101_0101_0000_0011_0010_0000_0101_1111 */
u32 a64_wfe(void)
{
    return (u32)0xD503205F;
}

/* WFI: 1101_0101_0000_0011_0010_0000_0111_1111 */
u32 a64_wfi(void)
{
    return (u32)0xD503207F;
}

/* YIELD: 1101_0101_0000_0011_0010_0000_0011_1111 */
u32 a64_yield(void)
{
    return (u32)0xD503203F;
}

/* SEV: 1101_0101_0000_0011_0010_0000_1001_1111 */
u32 a64_sev(void)
{
    return (u32)0xD503209F;
}

/* SEVL: 1101_0101_0000_0011_0010_0000_1011_1111 */
u32 a64_sevl(void)
{
    return (u32)0xD50320BF;
}

/* ---- Conditional select ---- */

/* CSEL Xd, Xn, Xm, cond: 1_00_11010100_Rm_cond_0_0_Rn_Rd */
u32 a64_csel(int rd, int rn, int rm, int cond)
{
    return (u32)0x9A800000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)cond & 0xF) << 12)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* CSINC Xd, Xn, Xm, cond: 1_00_11010100_Rm_cond_0_1_Rn_Rd */
u32 a64_csinc(int rd, int rn, int rm, int cond)
{
    return (u32)0x9A800400
         | (((u32)rm & 0x1F) << 16)
         | (((u32)cond & 0xF) << 12)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* CSINV Xd, Xn, Xm, cond: 1_10_11010100_Rm_cond_0_0_Rn_Rd */
u32 a64_csinv(int rd, int rn, int rm, int cond)
{
    return (u32)0xDA800000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)cond & 0xF) << 12)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* CSNEG Xd, Xn, Xm, cond: 1_10_11010100_Rm_cond_0_1_Rn_Rd */
u32 a64_csneg(int rd, int rn, int rm, int cond)
{
    return (u32)0xDA800400
         | (((u32)rm & 0x1F) << 16)
         | (((u32)cond & 0xF) << 12)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* ---- Test and branch ---- */

/* TBNZ Xt, #bit, label: 0_0110111_b5_bbbbb_imm14_Rt */
u32 a64_tbnz(int rt, int bit, i32 off14)
{
    u32 b5 = ((u32)bit >> 5) & 1;
    u32 b40 = (u32)bit & 0x1F;
    return (u32)0x37000000
         | (b5 << 31)
         | (b40 << 19)
         | (((u32)off14 & 0x3FFF) << 5)
         | ((u32)rt & 0x1F);
}

/* TBZ Xt, #bit, label: 0_0110110_b5_bbbbb_imm14_Rt */
u32 a64_tbz(int rt, int bit, i32 off14)
{
    u32 b5 = ((u32)bit >> 5) & 1;
    u32 b40 = (u32)bit & 0x1F;
    return (u32)0x36000000
         | (b5 << 31)
         | (b40 << 19)
         | (((u32)off14 & 0x3FFF) << 5)
         | ((u32)rt & 0x1F);
}

/* ---- Bit manipulation ---- */

/* CLZ Xd, Xn: 1_1_0_11010110_00000_00010_0_Rn_Rd */
u32 a64_clz(int rd, int rn)
{
    return (u32)0xDAC01000
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* RBIT Xd, Xn: 1_1_0_11010110_00000_00000_0_Rn_Rd */
u32 a64_rbit(int rd, int rn)
{
    return (u32)0xDAC00000
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* REV Xd, Xn (64-bit byte reverse): 1_1_0_11010110_00000_00001_1_Rn_Rd */
u32 a64_rev(int rd, int rn)
{
    return (u32)0xDAC00C00
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* REV16 Xd, Xn: 1_1_0_11010110_00000_00000_1_Rn_Rd */
u32 a64_rev16(int rd, int rn)
{
    return (u32)0xDAC00400
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* REV32 Xd, Xn: 1_1_0_11010110_00000_00001_0_Rn_Rd */
u32 a64_rev32(int rd, int rn)
{
    return (u32)0xDAC00800
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* ---- Bitfield instructions ---- */

/* UBFM Xd, Xn, #immr, #imms: 1_10_100110_1_immr_imms_Rn_Rd */
u32 a64_ubfm(int rd, int rn, int immr, int imms)
{
    return (u32)0xD3400000
         | (((u32)immr & 0x3F) << 16)
         | (((u32)imms & 0x3F) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* SBFM Xd, Xn, #immr, #imms: 1_00_100110_1_immr_imms_Rn_Rd */
u32 a64_sbfm(int rd, int rn, int immr, int imms)
{
    return (u32)0x93400000
         | (((u32)immr & 0x3F) << 16)
         | (((u32)imms & 0x3F) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* BFM Xd, Xn, #immr, #imms: 1_01_100110_1_immr_imms_Rn_Rd */
u32 a64_bfm(int rd, int rn, int immr, int imms)
{
    return (u32)0xB3400000
         | (((u32)immr & 0x3F) << 16)
         | (((u32)imms & 0x3F) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* ---- Logical immediate ---- */

/*
 * Encode a 64-bit bitmask immediate into N:immr:imms fields.
 * Returns 1 on success (fills *encoding), 0 if not representable.
 *
 * AArch64 logical immediates encode repeating bit patterns of width
 * 2, 4, 8, 16, 32, or 64 bits. The encoding is:
 *   N=1 for 64-bit element size, N=0 for smaller
 *   imms encodes (element_size - 1) in high bits + (ones - 1) in low bits
 *   immr encodes the rotation amount
 */
static int encode_bitmask_imm(u64 val, u32 *encoding)
{
    int size;
    u64 mask;
    u32 immr, imms, n;
    int ones, rot;
    u64 pat;
    int i;

    if (val == 0 || val == ~(u64)0) return 0;

    /* find the smallest repeating element size */
    for (size = 2; size < 64; size <<= 1) {
        mask = ((u64)1 << size) - 1;
        pat = val & mask;
        /* check if val is the pattern repeated across 64 bits */
        for (i = size; i < 64; i += size) {
            if (((val >> i) & mask) != pat) break;
        }
        if (i >= 64) break;
    }
    if (size < 2) size = 64;
    if (size > 64) size = 64;
    mask = (size == 64) ? ~(u64)0 : (((u64)1 << size) - 1);
    pat = val & mask;

    /* rotate to find the canonical form: contiguous ones at the bottom */
    rot = 0;
    {
        u64 tmp = pat;
        /* rotate right until we get trailing ones followed by leading zeros */
        for (rot = 0; rot < size; rot++) {
            /* count trailing ones */
            ones = 0;
            for (i = 0; i < size; i++) {
                if (tmp & ((u64)1 << i)) ones++;
                else break;
            }
            /* check that the rest are zeros */
            if (ones > 0 && ones < size) {
                int zeros = 0;
                for (i = ones; i < size; i++) {
                    if (!(tmp & ((u64)1 << i))) zeros++;
                    else break;
                }
                if (ones + zeros == size) break;
            }
            /* rotate right by 1 */
            {
                u64 lsb = tmp & 1;
                tmp = ((tmp >> 1) | (lsb << (size - 1))) & mask;
            }
        }
        if (rot >= size) return 0;
    }

    /* encode: immr is the inverse rotation (canonical -> original) */
    immr = (u32)((size - rot) % size) & (u32)(size - 1);
    n = (size == 64) ? 1 : 0;

    /* imms: the low bits encode (ones-1), the upper bits encode the
     * element size as an inverted mask:
     *   64-bit: N=1, imms = 0b0xxxxx (ones-1)
     *   32-bit: N=0, imms = 0b10xxxx
     *   16-bit: N=0, imms = 0b110xxx
     *    8-bit: N=0, imms = 0b1110xx
     *    4-bit: N=0, imms = 0b11110x
     *    2-bit: N=0, imms = 0b111100
     */
    {
        u32 size_mask;
        switch (size) {
            case 2:  size_mask = 0x3C; break;
            case 4:  size_mask = 0x38; break;
            case 8:  size_mask = 0x30; break;
            case 16: size_mask = 0x20; break;
            case 32: size_mask = 0x00; break;
            default: size_mask = 0x00; break; /* 64-bit uses N=1 */
        }
        imms = size_mask | ((u32)(ones - 1) & (u32)(size - 1));
    }

    *encoding = (n << 12) | (immr << 6) | imms;
    return 1;
}

/* AND Xd, Xn, #bitmask: 1_00_100100_N_immr_imms_Rn_Rd */
u32 a64_and_i(int rd, int rn, u64 bitmask)
{
    u32 enc = 0;
    encode_bitmask_imm(bitmask, &enc);
    return (u32)0x92000000
         | ((enc & 0x1FFF) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* ORR Xd, Xn, #bitmask: 1_01_100100_N_immr_imms_Rn_Rd */
u32 a64_orr_i(int rd, int rn, u64 bitmask)
{
    u32 enc = 0;
    encode_bitmask_imm(bitmask, &enc);
    return (u32)0xB2000000
         | ((enc & 0x1FFF) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* EOR Xd, Xn, #bitmask: 1_10_100100_N_immr_imms_Rn_Rd */
u32 a64_eor_i(int rd, int rn, u64 bitmask)
{
    u32 enc = 0;
    encode_bitmask_imm(bitmask, &enc);
    return (u32)0xD2000000
         | ((enc & 0x1FFF) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* ---- Pre/post-index addressing ---- */

/* LDR Xt, [Xn, #off]! (pre-index): 11_111000_01_0_off9_11_Rn_Rt */
u32 a64_ldr_pre(int rt, int rn, i32 off9)
{
    return (u32)0xF8400C00
         | (((u32)off9 & 0x1FF) << 12)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* STR Xt, [Xn], #off (post-index): 11_111000_00_0_off9_01_Rn_Rt */
u32 a64_str_post(int rt, int rn, i32 off9)
{
    return (u32)0xF8000400
         | (((u32)off9 & 0x1FF) << 12)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* LDR Xt, [Xn, Xm] (register offset): 11_111000_01_1_Rm_011_0_10_Rn_Rt */
u32 a64_ldr_r(int rt, int rn, int rm)
{
    return (u32)0xF8606800
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* STR Xt, [Xn, #off]! (pre-index): 11_111000_00_0_off9_11_Rn_Rt */
u32 a64_str_pre(int rt, int rn, i32 off9)
{
    return (u32)0xF8000C00
         | (((u32)off9 & 0x1FF) << 12)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* LDR Xt, [Xn], #off (post-index): 11_111000_01_0_off9_01_Rn_Rt */
u32 a64_ldr_post(int rt, int rn, i32 off9)
{
    return (u32)0xF8400400
         | (((u32)off9 & 0x1FF) << 12)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* LDR Dt, [Xn, #off]! (pre-index): 11_111100_01_0_off9_11_Rn_Rt */
u32 a64_ldr_d_pre(int rt, int rn, i32 off9)
{
    return (u32)0xFC400C00
         | (((u32)off9 & 0x1FF) << 12)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* STR Dt, [Xn, #off]! (pre-index): 11_111100_00_0_off9_11_Rn_Rt */
u32 a64_str_d_pre(int rt, int rn, i32 off9)
{
    return (u32)0xFC000C00
         | (((u32)off9 & 0x1FF) << 12)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* LDR Dt, [Xn], #off (post-index): 11_111100_01_0_off9_01_Rn_Rt */
u32 a64_ldr_d_post(int rt, int rn, i32 off9)
{
    return (u32)0xFC400400
         | (((u32)off9 & 0x1FF) << 12)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* STR Dt, [Xn], #off (post-index): 11_111100_00_0_off9_01_Rn_Rt */
u32 a64_str_d_post(int rt, int rn, i32 off9)
{
    return (u32)0xFC000400
         | (((u32)off9 & 0x1FF) << 12)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* STR Xt, [Xn, Xm] (register offset): 11_111000_00_1_Rm_011_0_10_Rn_Rt */
u32 a64_str_r(int rt, int rn, int rm)
{
    return (u32)0xF8206800
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* ---- BTI/PAC instructions ---- */

/* BTI c:  0xD503245F */
/* BTI j:  0xD503249F */
/* BTI jc: 0xD50324DF */
u32 a64_bti(int variant)
{
    /* variant: 0=c, 1=j, 2=jc */
    switch (variant) {
        case 0: return (u32)0xD503245F; /* c */
        case 1: return (u32)0xD503249F; /* j */
        case 2: return (u32)0xD50324DF; /* jc */
    }
    return (u32)0xD503245F;
}

/* PACIASP: 0xD503233F */
u32 a64_paciasp(void)
{
    return (u32)0xD503233F;
}

/* AUTIASP: 0xD50323BF */
u32 a64_autiasp(void)
{
    return (u32)0xD50323BF;
}

/* ---- System instructions (SYS aliases) ---- */

/*
 * SYS #op1, Cn, Cm, #op2, Xt
 * Encoding: 0xD5080000 | op1<<16 | CRn<<12 | CRm<<8 | op2<<5 | Rt
 */
u32 a64_sys(int op1, int crn, int crm, int op2, int rt)
{
    return (u32)0xD5080000
         | (((u32)op1 & 0x7) << 16)
         | (((u32)crn & 0xF) << 12)
         | (((u32)crm & 0xF) << 8)
         | (((u32)op2 & 0x7) << 5)
         | ((u32)rt & 0x1F);
}

/* HINT #imm: 1101_0101_0000_0011_0010_0000_imm7_11111
 * = 0xD503201F | (imm7 << 5) */
u32 a64_hint(int imm)
{
    return (u32)0xD503201F
         | (((u32)imm & 0x7F) << 5);
}

/* CLREX: 1101_0101_0000_0011_0011_0100_0101_1111 = 0xD503305F
 * (with CRm=0 default) */
u32 a64_clrex(void)
{
    return (u32)0xD503305F;
}

/* ---- Load-acquire / Store-release (standalone) ---- */

/*
 * LDAR: size_001000_1_1_0_11111_1_11111_Rn_Rt
 * size: 11=64-bit, 10=32-bit, 01=16-bit(ldarh), 00=8-bit(ldarb)
 * Base: 0xC8DFFC00 for 64-bit
 */
u32 a64_ldar(int rt, int rn)
{
    return (u32)0xC8DFFC00
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

u32 a64_ldar_w(int rt, int rn)
{
    return (u32)0x88DFFC00
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

u32 a64_ldarb(int rt, int rn)
{
    return (u32)0x08DFFC00
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

u32 a64_ldarh(int rt, int rn)
{
    return (u32)0x48DFFC00
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/*
 * STLR: size_001000_1_0_0_11111_1_11111_Rn_Rt
 * Base: 0xC89FFC00 for 64-bit
 */
u32 a64_stlr(int rt, int rn)
{
    return (u32)0xC89FFC00
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

u32 a64_stlr_w(int rt, int rn)
{
    return (u32)0x889FFC00
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

u32 a64_stlrb(int rt, int rn)
{
    return (u32)0x089FFC00
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

u32 a64_stlrh(int rt, int rn)
{
    return (u32)0x489FFC00
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* ---- Conditional compare ---- */

/*
 * CCMP (immediate): sf_1_1_11010010_imm5_cond_1_0_Rn_0_nzcv
 * 64-bit: 0xFA400800 | imm5<<16 | cond<<12 | Rn<<5 | nzcv
 */
u32 a64_ccmp_i(int rn, int imm5, int nzcv, int cond)
{
    return (u32)0xFA400800
         | (((u32)imm5 & 0x1F) << 16)
         | (((u32)cond & 0xF) << 12)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)nzcv & 0xF);
}

/*
 * CCMP (register): sf_1_1_11010010_Rm_cond_0_0_Rn_0_nzcv
 * 64-bit: 0xFA400000 | Rm<<16 | cond<<12 | Rn<<5 | nzcv
 */
u32 a64_ccmp_r(int rn, int rm, int nzcv, int cond)
{
    return (u32)0xFA400000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)cond & 0xF) << 12)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)nzcv & 0xF);
}

/*
 * CCMN (immediate): sf_0_1_11010010_imm5_cond_1_0_Rn_0_nzcv
 * 64-bit: 0xBA400800 | imm5<<16 | cond<<12 | Rn<<5 | nzcv
 */
u32 a64_ccmn_i(int rn, int imm5, int nzcv, int cond)
{
    return (u32)0xBA400800
         | (((u32)imm5 & 0x1F) << 16)
         | (((u32)cond & 0xF) << 12)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)nzcv & 0xF);
}

/* ---- EXTR ---- */

/*
 * EXTR Xd, Xn, Xm, #lsb: 1_00_100111_1_0_Rm_imms_Rn_Rd
 * 64-bit: 0x93C00000 | Rm<<16 | imms<<10 | Rn<<5 | Rd
 */
u32 a64_extr(int rd, int rn, int rm, int lsb)
{
    return (u32)0x93C00000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)lsb & 0x3F) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* ---- PRFM ---- */

/*
 * PRFM type, [Xn, #off]: 11_111_0_01_10_imm12_Rn_Rt
 * Scaled by 8. type is 5-bit prefetch operation.
 */
u32 a64_prfm(int type, int rn, i32 offset)
{
    u32 uoff = (u32)(offset / 8) & 0xFFF;
    return (u32)0xF9800000
         | (uoff << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)type & 0x1F);
}

/* ---- LDRSB (to 64-bit) ---- */

/*
 * LDRSB Xt, [Xn, #off]: 00_111_0_01_10_imm12_Rn_Rt
 * Unsigned offset, byte-scaled (no shift).
 */
u32 a64_ldrsb(int rt, int rn, i32 offset)
{
    if (offset < 0) {
        u32 imm9 = (u32)offset & 0x1FF;
        return (u32)0x38800000
             | (imm9 << 12)
             | (((u32)rn & 0x1F) << 5)
             | ((u32)rt & 0x1F);
    } else {
        u32 uoff = (u32)offset & 0xFFF;
        return (u32)0x39800000
             | (uoff << 10)
             | (((u32)rn & 0x1F) << 5)
             | ((u32)rt & 0x1F);
    }
}

/* ---- LDRSH (to 64-bit) ---- */

/*
 * LDRSH Xt, [Xn, #off]: 01_111_0_01_10_imm12_Rn_Rt
 * Unsigned offset, scaled by 2.
 */
u32 a64_ldrsh(int rt, int rn, i32 offset)
{
    if (offset < 0 || (offset & 1) != 0) {
        u32 imm9 = (u32)offset & 0x1FF;
        return (u32)0x78800000
             | (imm9 << 12)
             | (((u32)rn & 0x1F) << 5)
             | ((u32)rt & 0x1F);
    } else {
        u32 uoff = (u32)(offset / 2) & 0xFFF;
        return (u32)0x79800000
             | (uoff << 10)
             | (((u32)rn & 0x1F) << 5)
             | ((u32)rt & 0x1F);
    }
}

/* ---- MADD ---- */

/*
 * MADD Xd, Xn, Xm, Xa: 1_00_11011_000_Rm_0_Ra_Rn_Rd
 * = 0x9B000000 | Rm<<16 | Ra<<10 | Rn<<5 | Rd
 */
u32 a64_madd(int rd, int rn, int rm, int ra)
{
    return (u32)0x9B000000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)ra & 0x1F) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* ---- SMADDL ---- */

/*
 * SMADDL Xd, Wn, Wm, Xa: 1_00_11011_001_Rm_0_Ra_Rn_Rd
 * = 0x9B200000 | Rm<<16 | Ra<<10 | Rn<<5 | Rd
 */
u32 a64_smaddl(int rd, int rn, int rm, int ra)
{
    return (u32)0x9B200000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)ra & 0x1F) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* ---- UMADDL ---- */

/*
 * UMADDL Xd, Wn, Wm, Xa: 1_00_11011_101_Rm_0_Ra_Rn_Rd
 * = 0x9BA00000 | Rm<<16 | Ra<<10 | Rn<<5 | Rd
 */
u32 a64_umaddl(int rd, int rn, int rm, int ra)
{
    return (u32)0x9BA00000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)ra & 0x1F) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* ---- ORN (OR-NOT register) ---- */
/* ORN: 1_01_01010_00_1_Rm_000000_Rn_Rd */
u32 a64_orn_r(int rd, int rn, int rm)
{
    return (u32)0xAA200000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* ---- ANDS (AND-set-flags register) ---- */
/* ANDS: 1_11_01010_00_0_Rm_000000_Rn_Rd */
u32 a64_ands_r(int rd, int rn, int rm)
{
    return (u32)0xEA000000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* ---- BICS (BIC-set-flags register) ---- */
/* BICS: 1_11_01010_00_1_Rm_000000_Rn_Rd */
u32 a64_bics_r(int rd, int rn, int rm)
{
    return (u32)0xEA200000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* ---- ANDS immediate ---- */
/* Uses same bitmask encoding as AND immediate, but opc=11 instead of 00 */
u32 a64_ands_i(int rd, int rn, u64 bitmask)
{
    /* Reuse the bitmask encoder from a64_and_i but with ANDS opcode.
     * AND imm = 1_00_100100_N_immr_imms_Rn_Rd (0x92000000)
     * ANDS imm = 1_11_100100_N_immr_imms_Rn_Rd (0xF2000000) */
    u32 and_enc = a64_and_i(rd, rn, bitmask);
    /* Change opc from 00 to 11: flip bits 30:29 */
    return (and_enc & ~(u32)0x60000000) | (u32)0x60000000;
}

/* ---- ADDS register ---- */
/* ADDS: 1_01_01011_00_0_Rm_000000_Rn_Rd */
u32 a64_adds_r(int rd, int rn, int rm)
{
    return (u32)0xAB000000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* ---- ADDS immediate ---- */
/* ADDS: 1_01_10001_sh_imm12_Rn_Rd */
u32 a64_adds_i(int rd, int rn, u32 imm12)
{
    return (u32)0xB1000000
         | ((imm12 & 0xFFF) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* ---- STNP (store non-temporal pair) ---- */
/* STNP: 1_01_01000_00_imm7_Rt2_Rn_Rt */
u32 a64_stnp(int rt1, int rt2, int rn, i32 offset)
{
    i32 imm7 = offset / 8;
    return (u32)0xA8000000
         | (((u32)imm7 & 0x7F) << 15)
         | (((u32)rt2 & 0x1F) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt1 & 0x1F);
}

/* ---- LDNP (load non-temporal pair) ---- */
/* LDNP: 1_01_01000_01_imm7_Rt2_Rn_Rt */
u32 a64_ldnp(int rt1, int rt2, int rn, i32 offset)
{
    i32 imm7 = offset / 8;
    return (u32)0xA8400000
         | (((u32)imm7 & 0x7F) << 15)
         | (((u32)rt2 & 0x1F) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt1 & 0x1F);
}

/* ---- Load-acquire RCpc (LDAPR) ---- */

/*
 * LDAPR Xt, [Xn]: 11_111000_1_0_1_11111_1100_00_Rn_Rt = 0xF8BFC000
 * (64-bit, FEAT_LRCPC)
 */
u32 a64_ldapr(int rt, int rn)
{
    return (u32)0xF8BFC000
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* LDAPR Wt, [Xn]: 10_111000_1_0_1_11111_1100_00_Rn_Rt = 0xB8BFC000 (32-bit) */
u32 a64_ldapr_w(int rt, int rn)
{
    return (u32)0xB8BFC000
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* LDAPRB Wt, [Xn]: 00_111000_1_0_1_11111_1100_00_Rn_Rt */
u32 a64_ldaprb(int rt, int rn)
{
    return (u32)0x38BFC000
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* LDAPRH Wt, [Xn]: 01_111000_1_0_1_11111_1100_00_Rn_Rt */
u32 a64_ldaprh(int rt, int rn)
{
    return (u32)0x78BFC000
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* ---- Exclusive byte/halfword load/store ---- */

/* LDXRB Wt, [Xn]: 00_001000_0_1_0_11111_0_11111_Rn_Rt */
u32 a64_ldxrb(int rt, int rn)
{
    return (u32)0x085F7C00
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* LDXRH Wt, [Xn]: 01_001000_0_1_0_11111_0_11111_Rn_Rt */
u32 a64_ldxrh(int rt, int rn)
{
    return (u32)0x485F7C00
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* STXRB Ws, Wt, [Xn]: 00_001000_0_0_0_Rs_0_11111_Rn_Rt */
u32 a64_stxrb(int rs, int rt, int rn)
{
    return (u32)0x08007C00
         | (((u32)rs & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* STXRH Ws, Wt, [Xn]: 01_001000_0_0_0_Rs_0_11111_Rn_Rt */
u32 a64_stxrh(int rs, int rt, int rn)
{
    return (u32)0x48007C00
         | (((u32)rs & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* ---- Exclusive acquire/release byte/halfword/32-bit ---- */

/* LDAXR Wt, [Xn]: 10_001000_0_1_0_11111_1_11111_Rn_Rt */
u32 a64_ldaxr_w(int rt, int rn)
{
    return (u32)0x885FFC00
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* LDAXRB Wt, [Xn]: 00_001000_0_1_0_11111_1_11111_Rn_Rt */
u32 a64_ldaxrb(int rt, int rn)
{
    return (u32)0x085FFC00
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* LDAXRH Wt, [Xn]: 01_001000_0_1_0_11111_1_11111_Rn_Rt */
u32 a64_ldaxrh(int rt, int rn)
{
    return (u32)0x485FFC00
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* STLXR Ws, Wt, [Xn]: 10_001000_0_0_0_Rs_1_11111_Rn_Rt */
u32 a64_stlxr_w(int rs, int rt, int rn)
{
    return (u32)0x8800FC00
         | (((u32)rs & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* STLXRB Ws, Wt, [Xn]: 00_001000_0_0_0_Rs_1_11111_Rn_Rt */
u32 a64_stlxrb(int rs, int rt, int rn)
{
    return (u32)0x0800FC00
         | (((u32)rs & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* STLXRH Ws, Wt, [Xn]: 01_001000_0_0_0_Rs_1_11111_Rn_Rt */
u32 a64_stlxrh(int rs, int rt, int rn)
{
    return (u32)0x4800FC00
         | (((u32)rs & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* ---- CASP (compare and swap pair) ---- */

/*
 * CASP Xs, X(s+1), Xt, X(t+1), [Xn]:
 * 0_1_001000_0_0_1_Rs_0_11111_Rn_Rt = 0x48207C00
 * (64-bit pair)
 */
u32 a64_casp(int rs, int rt, int rn)
{
    return (u32)0x48207C00
         | (((u32)rs & 0x1F) << 16)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rt & 0x1F);
}

/* ---- CCMN register form ---- */

/*
 * CCMN (register): sf_0_1_11010010_Rm_cond_0_0_Rn_0_nzcv
 * 64-bit: 0xBA400000 | Rm<<16 | cond<<12 | Rn<<5 | nzcv
 */
u32 a64_ccmn_r(int rn, int rm, int nzcv, int cond)
{
    return (u32)0xBA400000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)cond & 0xF) << 12)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)nzcv & 0xF);
}

/* ---- 32-bit CCMP/CCMN ---- */

/* CCMP Wn, #imm5, #nzcv, cond: 0_1_1_11010010_imm5_cond_1_0_Rn_0_nzcv */
u32 a64_ccmp_i_w(int rn, int imm5, int nzcv, int cond)
{
    return (u32)0x7A400800
         | (((u32)imm5 & 0x1F) << 16)
         | (((u32)cond & 0xF) << 12)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)nzcv & 0xF);
}

/* CCMP Wn, Wm, #nzcv, cond */
u32 a64_ccmp_r_w(int rn, int rm, int nzcv, int cond)
{
    return (u32)0x7A400000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)cond & 0xF) << 12)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)nzcv & 0xF);
}

/* CCMN Wn, #imm5, #nzcv, cond */
u32 a64_ccmn_i_w(int rn, int imm5, int nzcv, int cond)
{
    return (u32)0x3A400800
         | (((u32)imm5 & 0x1F) << 16)
         | (((u32)cond & 0xF) << 12)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)nzcv & 0xF);
}

/* CCMN Wn, Wm, #nzcv, cond */
u32 a64_ccmn_r_w(int rn, int rm, int nzcv, int cond)
{
    return (u32)0x3A400000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)cond & 0xF) << 12)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)nzcv & 0xF);
}

/* ---- 32-bit conditional select ---- */

/* CSEL Wd, Wn, Wm, cond: 0_00_11010100_Rm_cond_0_0_Rn_Rd */
u32 a64_csel_w(int rd, int rn, int rm, int cond)
{
    return (u32)0x1A800000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)cond & 0xF) << 12)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* CSINC Wd, Wn, Wm, cond: 0_00_11010100_Rm_cond_0_1_Rn_Rd */
u32 a64_csinc_w(int rd, int rn, int rm, int cond)
{
    return (u32)0x1A800400
         | (((u32)rm & 0x1F) << 16)
         | (((u32)cond & 0xF) << 12)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* CSINV Wd, Wn, Wm, cond: 0_10_11010100_Rm_cond_0_0_Rn_Rd */
u32 a64_csinv_w(int rd, int rn, int rm, int cond)
{
    return (u32)0x5A800000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)cond & 0xF) << 12)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* CSNEG Wd, Wn, Wm, cond: 0_10_11010100_Rm_cond_0_1_Rn_Rd */
u32 a64_csneg_w(int rd, int rn, int rm, int cond)
{
    return (u32)0x5A800400
         | (((u32)rm & 0x1F) << 16)
         | (((u32)cond & 0xF) << 12)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* ---- 32-bit bit manipulation ---- */

/* CLZ Wd, Wn: 0_1_0_11010110_00000_00010_0_Rn_Rd */
u32 a64_clz_w(int rd, int rn)
{
    return (u32)0x5AC01000
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* RBIT Wd, Wn: 0_1_0_11010110_00000_00000_0_Rn_Rd */
u32 a64_rbit_w(int rd, int rn)
{
    return (u32)0x5AC00000
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* REV Wd, Wn (32-bit byte reverse): 0_1_0_11010110_00000_00001_0_Rn_Rd */
u32 a64_rev_w(int rd, int rn)
{
    return (u32)0x5AC00800
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* REV16 Wd, Wn: 0_1_0_11010110_00000_00000_1_Rn_Rd */
u32 a64_rev16_w(int rd, int rn)
{
    return (u32)0x5AC00400
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* ---- 32-bit bitfield ---- */

/* UBFM Wd, Wn, #immr, #imms: 0_10_100110_0_immr_imms_Rn_Rd */
u32 a64_ubfm_w(int rd, int rn, int immr, int imms)
{
    return (u32)0x53000000
         | (((u32)immr & 0x3F) << 16)
         | (((u32)imms & 0x3F) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* SBFM Wd, Wn, #immr, #imms: 0_00_100110_0_immr_imms_Rn_Rd */
u32 a64_sbfm_w(int rd, int rn, int immr, int imms)
{
    return (u32)0x13000000
         | (((u32)immr & 0x3F) << 16)
         | (((u32)imms & 0x3F) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* BFM Wd, Wn, #immr, #imms: 0_01_100110_0_immr_imms_Rn_Rd */
u32 a64_bfm_w(int rd, int rn, int immr, int imms)
{
    return (u32)0x33000000
         | (((u32)immr & 0x3F) << 16)
         | (((u32)imms & 0x3F) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}

/* ---- 32-bit extract ---- */

/* EXTR Wd, Wn, Wm, #lsb: 0_00_100111_0_0_Rm_imms_Rn_Rd */
u32 a64_extr_w(int rd, int rn, int rm, int lsb)
{
    return (u32)0x13800000
         | (((u32)rm & 0x1F) << 16)
         | (((u32)lsb & 0x3F) << 10)
         | (((u32)rn & 0x1F) << 5)
         | ((u32)rd & 0x1F);
}
