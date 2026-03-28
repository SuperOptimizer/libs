/*
 * test_encode.c - Tests for AArch64 instruction encoding.
 * Verifies every a64_* encoder function produces correct 32-bit encodings.
 * Pure C89. No external dependencies.
 */

#include "../test.h"
#include "aarch64.h"

/* ===== MOVZ tests ===== */

TEST(movz_basic)
{
    /* MOVZ X0, #42: sf=1 opc=10 hw=00 imm16=42 Rd=0 */
    ASSERT_EQ(a64_movz(REG_X0, 42, 0), (long)0xD2800540);
}

TEST(movz_shift16)
{
    /* MOVZ X1, #0, LSL #16: hw=01 */
    ASSERT_EQ(a64_movz(REG_X1, 0, 16), (long)0xD2A00001);
}

TEST(movz_shift32)
{
    /* MOVZ X0, #0xFFFF, LSL #32: hw=10 */
    ASSERT_EQ(a64_movz(REG_X0, 0xFFFF, 32), (long)0xD2DFFFE0);
}

TEST(movz_shift48)
{
    /* MOVZ X0, #1, LSL #48: hw=11 */
    ASSERT_EQ(a64_movz(REG_X0, 1, 48), (long)0xD2E00020);
}

TEST(movz_different_reg)
{
    /* MOVZ X5, #100 */
    u32 enc = a64_movz(REG_X5, 100, 0);
    ASSERT_EQ(enc & 0x1F, REG_X5);
}

/* ===== MOVK tests ===== */

TEST(movk_basic)
{
    /* MOVK X0, #0x1234, LSL #16 */
    ASSERT_EQ(a64_movk(REG_X0, 0x1234, 16), (long)0xF2A24680);
}

TEST(movk_no_shift)
{
    /* MOVK X0, #0x5678, LSL #0 */
    u32 enc = a64_movk(REG_X0, 0x5678, 0);
    ASSERT_EQ(enc & 0xFFE00000, (long)0xF2800000);
    ASSERT_EQ(enc & 0x1F, REG_X0);
}

/* ===== MOVN tests ===== */

TEST(movn_basic)
{
    /* MOVN X0, #0 => loads ~0 = all ones = -1 */
    ASSERT_EQ(a64_movn(REG_X0, 0, 0), (long)0x92800000);
}

TEST(movn_with_imm)
{
    /* MOVN X1, #42 */
    u32 enc = a64_movn(REG_X1, 42, 0);
    ASSERT_EQ(enc & 0xFFE00000, (long)0x92800000);
    ASSERT_EQ(enc & 0x1F, REG_X1);
}

/* ===== ADD register tests ===== */

TEST(add_r_basic)
{
    /* ADD X0, X1, X2 */
    ASSERT_EQ(a64_add_r(REG_X0, REG_X1, REG_X2), (long)0x8B020020);
}

TEST(add_r_different_regs)
{
    /* ADD X3, X5, X7 */
    ASSERT_EQ(a64_add_r(REG_X3, REG_X5, REG_X7), (long)0x8B0700A3);
}

TEST(add_r_sp)
{
    /* ADD X0, SP, X1 => uses reg 31 for rn */
    u32 enc = a64_add_r(REG_X0, REG_SP, REG_X1);
    ASSERT_EQ((enc >> 5) & 0x1F, REG_SP);
}

/* ===== SUB register tests ===== */

TEST(sub_r_basic)
{
    /* SUB X0, X1, X2 */
    ASSERT_EQ(a64_sub_r(REG_X0, REG_X1, REG_X2), (long)0xCB020020);
}

TEST(sub_r_self)
{
    /* SUB X0, X0, X0 => zero X0 */
    u32 enc = a64_sub_r(REG_X0, REG_X0, REG_X0);
    ASSERT_EQ(enc & 0x1F, REG_X0);
    ASSERT_EQ((enc >> 5) & 0x1F, REG_X0);
    ASSERT_EQ((enc >> 16) & 0x1F, REG_X0);
}

/* ===== SUBS register tests ===== */

TEST(subs_r_basic)
{
    /* SUBS X0, X1, X2 */
    ASSERT_EQ(a64_subs_r(REG_X0, REG_X1, REG_X2), (long)0xEB020020);
}

TEST(cmp_r_macro)
{
    /* CMP X0, X1 = SUBS XZR, X0, X1 */
    u32 enc = a64_cmp_r(REG_X0, REG_X1);
    ASSERT_EQ(enc, (long)0xEB01001F);
    /* Rd must be XZR=31 */
    ASSERT_EQ(enc & 0x1F, REG_XZR);
}

/* ===== MUL tests ===== */

TEST(mul_basic)
{
    /* MUL X0, X1, X2 = MADD X0, X1, X2, XZR */
    ASSERT_EQ(a64_mul(REG_X0, REG_X1, REG_X2), (long)0x9B027C20);
}

TEST(mul_different_regs)
{
    /* MUL X3, X4, X5 */
    u32 enc = a64_mul(REG_X3, REG_X4, REG_X5);
    ASSERT_EQ(enc & 0x1F, REG_X3);
    ASSERT_EQ((enc >> 5) & 0x1F, REG_X4);
    ASSERT_EQ((enc >> 16) & 0x1F, REG_X5);
    /* Ra field (bits 14:10) must be XZR=31 for MUL */
    ASSERT_EQ((enc >> 10) & 0x1F, REG_XZR);
}

/* ===== SDIV tests ===== */

TEST(sdiv_basic)
{
    /* SDIV X0, X1, X2 */
    ASSERT_EQ(a64_sdiv(REG_X0, REG_X1, REG_X2), (long)0x9AC20C20);
}

/* ===== UDIV tests ===== */

TEST(udiv_basic)
{
    /* UDIV X0, X1, X2 */
    ASSERT_EQ(a64_udiv(REG_X0, REG_X1, REG_X2), (long)0x9AC20820);
}

/* ===== MSUB tests ===== */

TEST(msub_basic)
{
    /* MSUB X0, X1, X2, X3 => X3 - X1*X2 */
    ASSERT_EQ(a64_msub(REG_X0, REG_X1, REG_X2, REG_X3), (long)0x9B028C20);
}

TEST(msub_field_check)
{
    /* Verify each field is in the right position */
    u32 enc = a64_msub(REG_X4, REG_X5, REG_X6, REG_X7);
    ASSERT_EQ(enc & 0x1F, REG_X4);           /* Rd */
    ASSERT_EQ((enc >> 5) & 0x1F, REG_X5);    /* Rn */
    ASSERT_EQ((enc >> 16) & 0x1F, REG_X6);   /* Rm */
    ASSERT_EQ((enc >> 10) & 0x1F, REG_X7);   /* Ra */
}

/* ===== ADD immediate tests ===== */

TEST(add_i_basic)
{
    /* ADD X0, X1, #42 */
    ASSERT_EQ(a64_add_i(REG_X0, REG_X1, 42), (long)0x9100A820);
}

TEST(add_i_zero)
{
    /* ADD X0, X0, #0 => NOP-like */
    u32 enc = a64_add_i(REG_X0, REG_X0, 0);
    ASSERT_EQ(enc & 0xFFC00000, (long)0x91000000);
    ASSERT_EQ((enc >> 10) & 0xFFF, 0);
}

TEST(add_i_max)
{
    /* ADD X0, X0, #4095 (max imm12) */
    u32 enc = a64_add_i(REG_X0, REG_X0, 4095);
    ASSERT_EQ((enc >> 10) & 0xFFF, 4095);
}

/* ===== SUB immediate tests ===== */

TEST(sub_i_basic)
{
    /* SUB X0, X1, #42 */
    ASSERT_EQ(a64_sub_i(REG_X0, REG_X1, 42), (long)0xD100A820);
}

/* ===== SUBS immediate tests ===== */

TEST(subs_i_basic)
{
    /* CMP X0, #0 = SUBS XZR, X0, #0 */
    ASSERT_EQ(a64_subs_i(REG_XZR, REG_X0, 0), (long)0xF100001F);
}

TEST(cmp_i_macro)
{
    /* CMP X0, #42 = SUBS XZR, X0, #42 */
    u32 enc = a64_cmp_i(REG_X0, 42);
    ASSERT_EQ(enc & 0x1F, REG_XZR);
    ASSERT_EQ((enc >> 10) & 0xFFF, 42);
}

/* ===== AND register tests ===== */

TEST(and_r_basic)
{
    /* AND X0, X1, X2 */
    ASSERT_EQ(a64_and_r(REG_X0, REG_X1, REG_X2), (long)0x8A020020);
}

/* ===== ORR register tests ===== */

TEST(orr_r_basic)
{
    /* ORR X0, X1, X2 */
    ASSERT_EQ(a64_orr_r(REG_X0, REG_X1, REG_X2), (long)0xAA020020);
}

TEST(mov_macro)
{
    /* MOV X0, X1 = ORR X0, XZR, X1 */
    u32 enc = a64_mov(REG_X0, REG_X1);
    ASSERT_EQ(enc, (long)0xAA0103E0);
    /* Rn must be XZR */
    ASSERT_EQ((enc >> 5) & 0x1F, REG_XZR);
}

/* ===== EOR register tests ===== */

TEST(eor_r_basic)
{
    /* EOR X0, X1, X2 */
    ASSERT_EQ(a64_eor_r(REG_X0, REG_X1, REG_X2), (long)0xCA020020);
}

/* ===== Shift tests ===== */

TEST(lsl_basic)
{
    /* LSL X0, X1, X2 = LSLV */
    ASSERT_EQ(a64_lsl(REG_X0, REG_X1, REG_X2), (long)0x9AC22020);
}

TEST(lsr_basic)
{
    /* LSR X0, X1, X2 = LSRV */
    ASSERT_EQ(a64_lsr(REG_X0, REG_X1, REG_X2), (long)0x9AC22420);
}

TEST(asr_basic)
{
    /* ASR X0, X1, X2 = ASRV */
    ASSERT_EQ(a64_asr(REG_X0, REG_X1, REG_X2), (long)0x9AC22820);
}

/* ===== CSET tests ===== */

TEST(cset_eq)
{
    /*
     * CSET X0, EQ => CSINC X0, XZR, XZR, NE (invert of EQ=0 is NE=1)
     * cond^1 = 0^1 = 1, placed at bits 15:12
     */
    ASSERT_EQ(a64_cset(REG_X0, COND_EQ), (long)0x9A9F17E0);
}

TEST(cset_ne)
{
    /* CSET X0, NE => CSINC X0, XZR, XZR, EQ (invert of NE=1 is EQ=0) */
    ASSERT_EQ(a64_cset(REG_X0, COND_NE), (long)0x9A9F07E0);
}

TEST(cset_lt)
{
    /* CSET X0, LT => CSINC X0, XZR, XZR, GE (invert 0xB^1=0xA) */
    ASSERT_EQ(a64_cset(REG_X0, COND_LT), (long)0x9A9FA7E0);
}

TEST(cset_ge)
{
    /* CSET X0, GE => CSINC X0, XZR, XZR, LT (invert 0xA^1=0xB) */
    ASSERT_EQ(a64_cset(REG_X0, COND_GE), (long)0x9A9FB7E0);
}

TEST(cset_gt)
{
    /* CSET X0, GT => CSINC X0, XZR, XZR, LE (invert 0xC^1=0xD) */
    ASSERT_EQ(a64_cset(REG_X0, COND_GT), (long)0x9A9FD7E0);
}

TEST(cset_le)
{
    /* CSET X0, LE => CSINC X0, XZR, XZR, GT (invert 0xD^1=0xC) */
    ASSERT_EQ(a64_cset(REG_X0, COND_LE), (long)0x9A9FC7E0);
}

TEST(cset_different_reg)
{
    /* CSET X5, EQ */
    u32 enc = a64_cset(REG_X5, COND_EQ);
    ASSERT_EQ(enc & 0x1F, REG_X5);
}

/* ===== B (unconditional branch) tests ===== */

TEST(b_zero)
{
    /* B #0 (offset 0 words) */
    ASSERT_EQ(a64_b(0), (long)0x14000000);
}

TEST(b_forward)
{
    /* B #4 (offset 1 word forward) */
    ASSERT_EQ(a64_b(1), (long)0x14000001);
}

TEST(b_backward)
{
    /* B #-4 (offset -1 word backward) */
    u32 enc = a64_b(-1);
    /* -1 in 26-bit field = 0x03FFFFFF */
    ASSERT_EQ(enc, (long)0x17FFFFFF);
}

/* ===== BL tests ===== */

TEST(bl_zero)
{
    /* BL #0 */
    ASSERT_EQ(a64_bl(0), (long)0x94000000);
}

TEST(bl_forward)
{
    /* BL #8 (offset 2 words) */
    ASSERT_EQ(a64_bl(2), (long)0x94000002);
}

/* ===== B.cond tests ===== */

TEST(b_cond_eq_zero)
{
    /* B.EQ #0 */
    ASSERT_EQ(a64_b_cond(COND_EQ, 0), (long)0x54000000);
}

TEST(b_cond_ne_forward)
{
    /* B.NE (offset 3 words) */
    ASSERT_EQ(a64_b_cond(COND_NE, 3), (long)0x54000061);
}

TEST(b_cond_field_check)
{
    /* Verify cond is in bits 3:0, imm19 in bits 23:5 */
    u32 enc = a64_b_cond(COND_GE, 1);
    ASSERT_EQ(enc & 0xF, COND_GE);
    ASSERT_EQ((enc >> 5) & 0x7FFFF, 1);
}

/* ===== BR tests ===== */

TEST(br_x0)
{
    /* BR X0 */
    ASSERT_EQ(a64_br(REG_X0), (long)0xD61F0000);
}

TEST(br_x30)
{
    /* BR X30 (like manual RET) */
    ASSERT_EQ(a64_br(REG_LR), (long)0xD61F03C0);
}

/* ===== BLR tests ===== */

TEST(blr_x0)
{
    /* BLR X0 */
    ASSERT_EQ(a64_blr(REG_X0), (long)0xD63F0000);
}

TEST(blr_x16)
{
    /* BLR X16 (IP0, common for PLT calls) */
    u32 enc = a64_blr(REG_X16);
    ASSERT_EQ((enc >> 5) & 0x1F, REG_X16);
}

/* ===== RET tests ===== */

TEST(ret_encoding)
{
    /* RET = BR X30 variant */
    ASSERT_EQ(a64_ret(), (long)0xD65F03C0);
}

/* ===== LDR tests ===== */

TEST(ldr_zero_offset)
{
    /* LDR X0, [X1, #0] */
    ASSERT_EQ(a64_ldr(REG_X0, REG_X1, 0), (long)0xF9400020);
}

TEST(ldr_with_offset)
{
    /* LDR X0, [X1, #8] (imm12 = 8/8 = 1) */
    ASSERT_EQ(a64_ldr(REG_X0, REG_X1, 8), (long)0xF9400420);
}

TEST(ldr_large_offset)
{
    /* LDR X0, [X1, #16] (imm12 = 16/8 = 2) */
    u32 enc = a64_ldr(REG_X0, REG_X1, 16);
    ASSERT_EQ((enc >> 10) & 0xFFF, 2);
}

/* ===== STR tests ===== */

TEST(str_zero_offset)
{
    /* STR X0, [X1, #0] */
    ASSERT_EQ(a64_str(REG_X0, REG_X1, 0), (long)0xF9000020);
}

TEST(str_with_offset)
{
    /* STR X0, [X1, #16] (imm12 = 16/8 = 2) */
    ASSERT_EQ(a64_str(REG_X0, REG_X1, 16), (long)0xF9000820);
}

/* ===== LDRB tests ===== */

TEST(ldrb_zero_offset)
{
    /* LDRB W0, [X1, #0] */
    ASSERT_EQ(a64_ldrb(REG_X0, REG_X1, 0), (long)0x39400020);
}

TEST(ldrb_with_offset)
{
    /* LDRB W0, [X1, #5] (byte-scaled, imm12 = 5) */
    u32 enc = a64_ldrb(REG_X0, REG_X1, 5);
    ASSERT_EQ((enc >> 10) & 0xFFF, 5);
}

/* ===== STRB tests ===== */

TEST(strb_zero_offset)
{
    /* STRB W0, [X1, #0] */
    ASSERT_EQ(a64_strb(REG_X0, REG_X1, 0), (long)0x39000020);
}

/* ===== LDRH tests ===== */

TEST(ldrh_zero_offset)
{
    /* LDRH W0, [X1, #0] */
    ASSERT_EQ(a64_ldrh(REG_X0, REG_X1, 0), (long)0x79400020);
}

TEST(ldrh_with_offset)
{
    /* LDRH W0, [X1, #4] (half-scaled, imm12 = 4/2 = 2) */
    u32 enc = a64_ldrh(REG_X0, REG_X1, 4);
    ASSERT_EQ((enc >> 10) & 0xFFF, 2);
}

/* ===== STRH tests ===== */

TEST(strh_zero_offset)
{
    /* STRH W0, [X1, #0] */
    ASSERT_EQ(a64_strh(REG_X0, REG_X1, 0), (long)0x79000020);
}

/* ===== LDRSW tests ===== */

TEST(ldrsw_zero_offset)
{
    /* LDRSW X0, [X1, #0] */
    ASSERT_EQ(a64_ldrsw(REG_X0, REG_X1, 0), (long)0xB9800020);
}

TEST(ldrsw_with_offset)
{
    /* LDRSW X0, [X1, #4] (word-scaled, imm12 = 4/4 = 1) */
    u32 enc = a64_ldrsw(REG_X0, REG_X1, 4);
    ASSERT_EQ((enc >> 10) & 0xFFF, 1);
}

/* ===== LDR W (32-bit) tests ===== */

TEST(ldr_w_zero_offset)
{
    /* LDR W0, [X1, #0] */
    ASSERT_EQ(a64_ldr_w(REG_X0, REG_X1, 0), (long)0xB9400020);
}

/* ===== STR W (32-bit) tests ===== */

TEST(str_w_zero_offset)
{
    /* STR W0, [X1, #0] */
    ASSERT_EQ(a64_str_w(REG_X0, REG_X1, 0), (long)0xB9000020);
}

/* ===== STP pre-index tests ===== */

TEST(stp_pre_frame_push)
{
    /* STP X29, X30, [SP, #-16]! (standard prologue) */
    ASSERT_EQ(a64_stp_pre(REG_FP, REG_LR, REG_SP, -16), (long)0xA9BF7BFD);
}

TEST(stp_pre_field_check)
{
    /* Verify register fields */
    u32 enc = a64_stp_pre(REG_FP, REG_LR, REG_SP, -16);
    ASSERT_EQ(enc & 0x1F, REG_FP);              /* Rt1 */
    ASSERT_EQ((enc >> 5) & 0x1F, REG_SP);        /* Rn */
    ASSERT_EQ((enc >> 10) & 0x1F, REG_LR);       /* Rt2 */
}

/* ===== LDP post-index tests ===== */

TEST(ldp_post_frame_pop)
{
    /* LDP X29, X30, [SP], #16 (standard epilogue) */
    ASSERT_EQ(a64_ldp_post(REG_FP, REG_LR, REG_SP, 16), (long)0xA8C17BFD);
}

TEST(ldp_post_field_check)
{
    /* Verify register fields */
    u32 enc = a64_ldp_post(REG_FP, REG_LR, REG_SP, 16);
    ASSERT_EQ(enc & 0x1F, REG_FP);              /* Rt1 */
    ASSERT_EQ((enc >> 5) & 0x1F, REG_SP);        /* Rn */
    ASSERT_EQ((enc >> 10) & 0x1F, REG_LR);       /* Rt2 */
}

/* ===== STP signed offset tests ===== */

TEST(stp_signed_offset)
{
    /* STP X0, X1, [SP, #16] */
    ASSERT_EQ(a64_stp(REG_X0, REG_X1, REG_SP, 16), (long)0xA90107E0);
}

/* ===== LDP signed offset tests ===== */

TEST(ldp_signed_offset)
{
    /* LDP X0, X1, [SP, #16] */
    ASSERT_EQ(a64_ldp(REG_X0, REG_X1, REG_SP, 16), (long)0xA94107E0);
}

/* ===== ADRP tests ===== */

TEST(adrp_zero)
{
    /* ADRP X0, #0 */
    ASSERT_EQ(a64_adrp(REG_X0, 0), (long)0x90000000);
}

TEST(adrp_one)
{
    /* ADRP X1, #1 */
    ASSERT_EQ(a64_adrp(REG_X1, 1), (long)0xB0000001);
}

/* ===== ADR tests ===== */

TEST(adr_zero)
{
    /* ADR X0, #0 */
    ASSERT_EQ(a64_adr(REG_X0, 0), (long)0x10000000);
}

TEST(adr_four)
{
    /* ADR X1, #4 => immlo=0, immhi=1 */
    ASSERT_EQ(a64_adr(REG_X1, 4), (long)0x10000021);
}

/* ===== MVN tests ===== */

TEST(mvn_basic)
{
    /* MVN X0, X1 = ORN X0, XZR, X1 */
    ASSERT_EQ(a64_mvn(REG_X0, REG_X1), (long)0xAA2103E0);
}

TEST(mvn_field_check)
{
    /* Rn should be XZR=31 */
    u32 enc = a64_mvn(REG_X0, REG_X1);
    ASSERT_EQ((enc >> 5) & 0x1F, REG_XZR);
    ASSERT_EQ((enc >> 16) & 0x1F, REG_X1);
}

/* ===== SXTB tests ===== */

TEST(sxtb_basic)
{
    /* SXTB X0, W1 = SBFM X0, X1, #0, #7 */
    ASSERT_EQ(a64_sxtb(REG_X0, REG_X1), (long)0x93401C20);
}

/* ===== SXTH tests ===== */

TEST(sxth_basic)
{
    /* SXTH X0, W1 = SBFM X0, X1, #0, #15 */
    ASSERT_EQ(a64_sxth(REG_X0, REG_X1), (long)0x93403C20);
}

/* ===== SXTW tests ===== */

TEST(sxtw_basic)
{
    /* SXTW X0, W1 = SBFM X0, X1, #0, #31 */
    ASSERT_EQ(a64_sxtw(REG_X0, REG_X1), (long)0x93407C20);
}

/* ===== UXTB tests ===== */

TEST(uxtb_basic)
{
    /* UXTB W0, W1 = UBFM W0, W1, #0, #7 */
    ASSERT_EQ(a64_uxtb(REG_X0, REG_X1), (long)0x53001C20);
}

/* ===== UXTH tests ===== */

TEST(uxth_basic)
{
    /* UXTH W0, W1 = UBFM W0, W1, #0, #15 */
    ASSERT_EQ(a64_uxth(REG_X0, REG_X1), (long)0x53003C20);
}

/* ===== System instruction tests ===== */

TEST(nop_encoding)
{
    /* NOP */
    ASSERT_EQ(a64_nop(), (long)0xD503201F);
}

TEST(svc_zero)
{
    /* SVC #0 */
    ASSERT_EQ(a64_svc(0), (long)0xD4000001);
}

TEST(svc_with_imm)
{
    /* SVC #1 => imm16 shifted left by 5 */
    u32 enc = a64_svc(1);
    ASSERT_EQ(enc, (long)0xD4000021);
}

/* ===== NEG macro tests ===== */

TEST(neg_macro)
{
    /* NEG X0, X1 = SUB X0, XZR, X1 */
    u32 enc = a64_neg(REG_X0, REG_X1);
    ASSERT_EQ(enc, (long)0xCB0103E0);
    ASSERT_EQ((enc >> 5) & 0x1F, REG_XZR);
}

/* ===== Register encoding boundary tests ===== */

TEST(high_register_rd)
{
    /* ADD X28, X0, X0 => verify Rd=28 */
    u32 enc = a64_add_r(REG_X28, REG_X0, REG_X0);
    ASSERT_EQ(enc & 0x1F, 28);
}

TEST(high_register_rn)
{
    /* ADD X0, X28, X0 => verify Rn=28 */
    u32 enc = a64_add_r(REG_X0, REG_X28, REG_X0);
    ASSERT_EQ((enc >> 5) & 0x1F, 28);
}

TEST(high_register_rm)
{
    /* ADD X0, X0, X28 => verify Rm=28 */
    u32 enc = a64_add_r(REG_X0, REG_X0, REG_X28);
    ASSERT_EQ((enc >> 16) & 0x1F, 28);
}

TEST(xzr_as_rd)
{
    /* SUBS XZR, X0, X1 => CMP, Rd=31 */
    u32 enc = a64_subs_r(REG_XZR, REG_X0, REG_X1);
    ASSERT_EQ(enc & 0x1F, 31);
}

int main(void)
{
    printf("test_encode:\n");

    /* MOVZ */
    RUN_TEST(movz_basic);
    RUN_TEST(movz_shift16);
    RUN_TEST(movz_shift32);
    RUN_TEST(movz_shift48);
    RUN_TEST(movz_different_reg);

    /* MOVK */
    RUN_TEST(movk_basic);
    RUN_TEST(movk_no_shift);

    /* MOVN */
    RUN_TEST(movn_basic);
    RUN_TEST(movn_with_imm);

    /* ADD register */
    RUN_TEST(add_r_basic);
    RUN_TEST(add_r_different_regs);
    RUN_TEST(add_r_sp);

    /* SUB register */
    RUN_TEST(sub_r_basic);
    RUN_TEST(sub_r_self);

    /* SUBS register */
    RUN_TEST(subs_r_basic);
    RUN_TEST(cmp_r_macro);

    /* MUL */
    RUN_TEST(mul_basic);
    RUN_TEST(mul_different_regs);

    /* SDIV */
    RUN_TEST(sdiv_basic);

    /* UDIV */
    RUN_TEST(udiv_basic);

    /* MSUB */
    RUN_TEST(msub_basic);
    RUN_TEST(msub_field_check);

    /* ADD immediate */
    RUN_TEST(add_i_basic);
    RUN_TEST(add_i_zero);
    RUN_TEST(add_i_max);

    /* SUB immediate */
    RUN_TEST(sub_i_basic);

    /* SUBS immediate */
    RUN_TEST(subs_i_basic);
    RUN_TEST(cmp_i_macro);

    /* AND */
    RUN_TEST(and_r_basic);

    /* ORR */
    RUN_TEST(orr_r_basic);
    RUN_TEST(mov_macro);

    /* EOR */
    RUN_TEST(eor_r_basic);

    /* Shift */
    RUN_TEST(lsl_basic);
    RUN_TEST(lsr_basic);
    RUN_TEST(asr_basic);

    /* CSET */
    RUN_TEST(cset_eq);
    RUN_TEST(cset_ne);
    RUN_TEST(cset_lt);
    RUN_TEST(cset_ge);
    RUN_TEST(cset_gt);
    RUN_TEST(cset_le);
    RUN_TEST(cset_different_reg);

    /* B */
    RUN_TEST(b_zero);
    RUN_TEST(b_forward);
    RUN_TEST(b_backward);

    /* BL */
    RUN_TEST(bl_zero);
    RUN_TEST(bl_forward);

    /* B.cond */
    RUN_TEST(b_cond_eq_zero);
    RUN_TEST(b_cond_ne_forward);
    RUN_TEST(b_cond_field_check);

    /* BR */
    RUN_TEST(br_x0);
    RUN_TEST(br_x30);

    /* BLR */
    RUN_TEST(blr_x0);
    RUN_TEST(blr_x16);

    /* RET */
    RUN_TEST(ret_encoding);

    /* LDR */
    RUN_TEST(ldr_zero_offset);
    RUN_TEST(ldr_with_offset);
    RUN_TEST(ldr_large_offset);

    /* STR */
    RUN_TEST(str_zero_offset);
    RUN_TEST(str_with_offset);

    /* LDRB */
    RUN_TEST(ldrb_zero_offset);
    RUN_TEST(ldrb_with_offset);

    /* STRB */
    RUN_TEST(strb_zero_offset);

    /* LDRH */
    RUN_TEST(ldrh_zero_offset);
    RUN_TEST(ldrh_with_offset);

    /* STRH */
    RUN_TEST(strh_zero_offset);

    /* LDRSW */
    RUN_TEST(ldrsw_zero_offset);
    RUN_TEST(ldrsw_with_offset);

    /* LDR W */
    RUN_TEST(ldr_w_zero_offset);

    /* STR W */
    RUN_TEST(str_w_zero_offset);

    /* STP pre-index */
    RUN_TEST(stp_pre_frame_push);
    RUN_TEST(stp_pre_field_check);

    /* LDP post-index */
    RUN_TEST(ldp_post_frame_pop);
    RUN_TEST(ldp_post_field_check);

    /* STP signed offset */
    RUN_TEST(stp_signed_offset);

    /* LDP signed offset */
    RUN_TEST(ldp_signed_offset);

    /* ADRP */
    RUN_TEST(adrp_zero);
    RUN_TEST(adrp_one);

    /* ADR */
    RUN_TEST(adr_zero);
    RUN_TEST(adr_four);

    /* MVN */
    RUN_TEST(mvn_basic);
    RUN_TEST(mvn_field_check);

    /* Sign extend */
    RUN_TEST(sxtb_basic);
    RUN_TEST(sxth_basic);
    RUN_TEST(sxtw_basic);

    /* Zero extend */
    RUN_TEST(uxtb_basic);
    RUN_TEST(uxth_basic);

    /* System */
    RUN_TEST(nop_encoding);
    RUN_TEST(svc_zero);
    RUN_TEST(svc_with_imm);

    /* Macros */
    RUN_TEST(neg_macro);

    /* Register boundary */
    RUN_TEST(high_register_rd);
    RUN_TEST(high_register_rn);
    RUN_TEST(high_register_rm);
    RUN_TEST(xzr_as_rd);

    TEST_SUMMARY();
    return tests_failed;
}
