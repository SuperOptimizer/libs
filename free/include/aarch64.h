/*
 * aarch64.h - AArch64 instruction encoding for the free toolchain
 * Helpers to emit machine code for the minimal instruction set
 */
#ifndef FREE_AARCH64_H
#define FREE_AARCH64_H

#include "free.h"

/* ---- Register numbers ---- */
#define REG_X0   0
#define REG_X1   1
#define REG_X2   2
#define REG_X3   3
#define REG_X4   4
#define REG_X5   5
#define REG_X6   6
#define REG_X7   7
#define REG_X8   8    /* syscall number / indirect result */
#define REG_X9   9
#define REG_X10  10
#define REG_X11  11
#define REG_X12  12
#define REG_X13  13
#define REG_X14  14
#define REG_X15  15
#define REG_X16  16   /* IP0 scratch */
#define REG_X17  17   /* IP1 scratch */
#define REG_X18  18   /* platform register (avoid) */
#define REG_X19  19
#define REG_X20  20
#define REG_X21  21
#define REG_X22  22
#define REG_X23  23
#define REG_X24  24
#define REG_X25  25
#define REG_X26  26
#define REG_X27  27
#define REG_X28  28
#define REG_FP   29   /* frame pointer */
#define REG_LR   30   /* link register */
#define REG_SP   31   /* stack pointer (context-dependent with XZR) */
#define REG_XZR  31   /* zero register */

/* ---- FP/SIMD register numbers (same encoding as Xn, context-dependent) ---- */
#define REG_D0   0
#define REG_D1   1
#define REG_D2   2
#define REG_D3   3
#define REG_D4   4
#define REG_D5   5
#define REG_D6   6
#define REG_D7   7
#define REG_D8   8
#define REG_D9   9
#define REG_D10  10
#define REG_D11  11
#define REG_D12  12
#define REG_D13  13
#define REG_D14  14
#define REG_D15  15
#define REG_D16  16
#define REG_D17  17
#define REG_D18  18
#define REG_D19  19
#define REG_D20  20
#define REG_D21  21
#define REG_D22  22
#define REG_D23  23
#define REG_D24  24
#define REG_D25  25
#define REG_D26  26
#define REG_D27  27
#define REG_D28  28
#define REG_D29  29
#define REG_D30  30
#define REG_D31  31

/* ---- Condition codes ---- */
#define COND_EQ  0x0
#define COND_NE  0x1
#define COND_CS  0x2  /* HS (unsigned >=) */
#define COND_CC  0x3  /* LO (unsigned <) */
#define COND_MI  0x4  /* negative */
#define COND_PL  0x5  /* positive or zero */
#define COND_VS  0x6  /* overflow */
#define COND_VC  0x7  /* no overflow */
#define COND_HI  0x8  /* unsigned > */
#define COND_LS  0x9  /* unsigned <= */
#define COND_GE  0xA  /* signed >= */
#define COND_LT  0xB  /* signed < */
#define COND_GT  0xC  /* signed > */
#define COND_LE  0xD  /* signed <= */
#define COND_AL  0xE  /* always */

/* ---- Syscall numbers (Linux aarch64) ---- */
#define SYS_OPENAT     56
#define SYS_CLOSE      57
#define SYS_LSEEK      62
#define SYS_READ       63
#define SYS_WRITE      64
#define SYS_FSTATAT    79
#define SYS_EXIT_GROUP 94
#define SYS_MUNMAP     215
#define SYS_BRK        214
#define SYS_MMAP       222

/* ---- Instruction encoders ---- */

/* Data processing - immediate */
u32 a64_movz(int rd, u16 imm, int shift);   /* MOVZ Xd, #imm, LSL #shift */
u32 a64_movk(int rd, u16 imm, int shift);   /* MOVK Xd, #imm, LSL #shift */
u32 a64_movn(int rd, u16 imm, int shift);   /* MOVN Xd, #imm, LSL #shift */
u32 a64_movz_w(int rd, u16 imm, int shift); /* MOVZ Wd, #imm, LSL #shift */
u32 a64_movk_w(int rd, u16 imm, int shift); /* MOVK Wd, #imm, LSL #shift */
u32 a64_movn_w(int rd, u16 imm, int shift); /* MOVN Wd, #imm, LSL #shift */

/* Arithmetic - register */
u32 a64_add_r(int rd, int rn, int rm);      /* ADD Xd, Xn, Xm */
u32 a64_sub_r(int rd, int rn, int rm);      /* SUB Xd, Xn, Xm */
u32 a64_subs_r(int rd, int rn, int rm);     /* SUBS Xd, Xn, Xm (sets flags) */
u32 a64_mul(int rd, int rn, int rm);        /* MUL Xd, Xn, Xm */
u32 a64_sdiv(int rd, int rn, int rm);       /* SDIV Xd, Xn, Xm */
u32 a64_udiv(int rd, int rn, int rm);       /* UDIV Xd, Xn, Xm */
u32 a64_msub(int rd, int rn, int rm, int ra); /* MSUB Xd, Xn, Xm, Xa (ra-rn*rm) */

/* Arithmetic - immediate */
u32 a64_add_i(int rd, int rn, u32 imm12);   /* ADD Xd, Xn, #imm12 */
u32 a64_sub_i(int rd, int rn, u32 imm12);   /* SUB Xd, Xn, #imm12 */
u32 a64_subs_i(int rd, int rn, u32 imm12);  /* SUBS Xd, Xn, #imm12 */

/* Logical - register */
u32 a64_and_r(int rd, int rn, int rm);      /* AND Xd, Xn, Xm */
u32 a64_orr_r(int rd, int rn, int rm);      /* ORR Xd, Xn, Xm */
u32 a64_eor_r(int rd, int rn, int rm);      /* EOR Xd, Xn, Xm */

/* Shift */
u32 a64_lsl(int rd, int rn, int rm);        /* LSL Xd, Xn, Xm */
u32 a64_lsr(int rd, int rn, int rm);        /* LSR Xd, Xn, Xm */
u32 a64_asr(int rd, int rn, int rm);        /* ASR Xd, Xn, Xm */

/* Compare */
#define a64_cmp_r(rn, rm)   a64_subs_r(REG_XZR, rn, rm)
#define a64_cmp_i(rn, imm)  a64_subs_i(REG_XZR, rn, imm)

/* Conditional set */
u32 a64_cset(int rd, int cond);             /* CSET Xd, cond */

/* Branch */
u32 a64_b(i32 offset);                      /* B offset (words) */
u32 a64_bl(i32 offset);                     /* BL offset (words) */
u32 a64_b_cond(int cond, i32 offset);       /* B.cond offset (words) */
u32 a64_br(int rn);                         /* BR Xn */
u32 a64_blr(int rn);                        /* BLR Xn */
u32 a64_ret(void);                          /* RET (BR X30) */

/* Load/Store */
u32 a64_ldr(int rt, int rn, i32 offset);    /* LDR Xt, [Xn, #off] */
u32 a64_str(int rt, int rn, i32 offset);    /* STR Xt, [Xn, #off] */
u32 a64_ldrb(int rt, int rn, i32 offset);   /* LDRB Wt, [Xn, #off] */
u32 a64_strb(int rt, int rn, i32 offset);   /* STRB Wt, [Xn, #off] */
u32 a64_ldrh(int rt, int rn, i32 offset);   /* LDRH Wt, [Xn, #off] */
u32 a64_strh(int rt, int rn, i32 offset);   /* STRH Wt, [Xn, #off] */
u32 a64_ldrsw(int rt, int rn, i32 offset);  /* LDRSW Xt, [Xn, #off] */
u32 a64_ldr_w(int rt, int rn, i32 offset);  /* LDR Wt, [Xn, #off] */
u32 a64_str_w(int rt, int rn, i32 offset);  /* STR Wt, [Xn, #off] */

/* Load/Store pair */
u32 a64_stp_pre(int rt1, int rt2, int rn, i32 offset);  /* STP Xt1, Xt2, [Xn, #off]! */
u32 a64_stp_post(int rt1, int rt2, int rn, i32 offset); /* STP Xt1, Xt2, [Xn], #off */
u32 a64_ldp_post(int rt1, int rt2, int rn, i32 offset); /* LDP Xt1, Xt2, [Xn], #off */
u32 a64_ldp_pre(int rt1, int rt2, int rn, i32 offset);  /* LDP Xt1, Xt2, [Xn, #off]! */
u32 a64_stp(int rt1, int rt2, int rn, i32 offset);      /* STP Xt1, Xt2, [Xn, #off] */
u32 a64_ldp(int rt1, int rt2, int rn, i32 offset);      /* LDP Xt1, Xt2, [Xn, #off] */

/* Address */
u32 a64_adrp(int rd, i32 offset);           /* ADRP Xd, offset (pages) */
u32 a64_adr(int rd, i32 offset);            /* ADR Xd, offset (bytes) */

/* Move register */
#define a64_mov(rd, rm) a64_orr_r(rd, REG_XZR, rm)

/* Negate */
#define a64_neg(rd, rm) a64_sub_r(rd, REG_XZR, rm)

/* MVN (bitwise NOT) */
u32 a64_mvn(int rd, int rm);                /* MVN Xd, Xm */

/* Sign/Zero extend */
u32 a64_sxtb(int rd, int rn);              /* SXTB Xd, Wn */
u32 a64_sxth(int rd, int rn);              /* SXTH Xd, Wn */
u32 a64_sxtw(int rd, int rn);              /* SXTW Xd, Wn */
u32 a64_uxtb(int rd, int rn);              /* UXTB Wd, Wn */
u32 a64_uxth(int rd, int rn);              /* UXTH Wd, Wn */

/* Atomic/Exclusive */
u32 a64_ldxr(int rt, int rn);                 /* LDXR Xt, [Xn] (64-bit) */
u32 a64_ldxr_w(int rt, int rn);              /* LDXR Wt, [Xn] (32-bit) */
u32 a64_ldxrb(int rt, int rn);               /* LDXRB Wt, [Xn] */
u32 a64_ldxrh(int rt, int rn);               /* LDXRH Wt, [Xn] */
u32 a64_stxr(int rs, int rt, int rn);         /* STXR Ws, Xt, [Xn] (64-bit) */
u32 a64_stxr_w(int rs, int rt, int rn);      /* STXR Ws, Wt, [Xn] (32-bit) */
u32 a64_stxrb(int rs, int rt, int rn);       /* STXRB Ws, Wt, [Xn] */
u32 a64_stxrh(int rs, int rt, int rn);       /* STXRH Ws, Wt, [Xn] */
u32 a64_ldaxr(int rt, int rn);                /* LDAXR Xt, [Xn] */
u32 a64_ldaxr_w(int rt, int rn);             /* LDAXR Wt, [Xn] */
u32 a64_ldaxrb(int rt, int rn);              /* LDAXRB Wt, [Xn] */
u32 a64_ldaxrh(int rt, int rn);              /* LDAXRH Wt, [Xn] */
u32 a64_stlxr(int rs, int rt, int rn);        /* STLXR Ws, Xt, [Xn] */
u32 a64_stlxr_w(int rs, int rt, int rn);     /* STLXR Ws, Wt, [Xn] */
u32 a64_stlxrb(int rs, int rt, int rn);      /* STLXRB Ws, Wt, [Xn] */
u32 a64_stlxrh(int rs, int rt, int rn);      /* STLXRH Ws, Wt, [Xn] */
u32 a64_ldadd(int rs, int rt, int rn);        /* LDADD Xs, Xt, [Xn] (LSE) */
u32 a64_stadd(int rs, int rn);                /* STADD Xs, [Xn] (LSE) */
u32 a64_swp(int rs, int rt, int rn);          /* SWP Xs, Xt, [Xn] (LSE) */
u32 a64_cas(int rs, int rt, int rn);          /* CAS Xs, Xt, [Xn] (LSE) */
u32 a64_casp(int rs, int rt, int rn);         /* CASP Xs, Xs+1, Xt, Xt+1, [Xn] */

/* Barriers */
u32 a64_dmb(int option);                      /* DMB option */
u32 a64_dsb(int option);                      /* DSB option */
u32 a64_isb(void);                            /* ISB */

/* Barrier option constants */
#define BARRIER_OSHLD  0x1
#define BARRIER_OSHST  0x2
#define BARRIER_OSH    0x3
#define BARRIER_NSHLD  0x5
#define BARRIER_NSHST  0x6
#define BARRIER_NSH    0x7
#define BARRIER_ISHLD  0x9
#define BARRIER_ISHST  0xA
#define BARRIER_ISH    0xB
#define BARRIER_LD     0xD
#define BARRIER_ST     0xE
#define BARRIER_SY     0xF

/* System registers: o0(2):op1(3):CRn(4):CRm(4):op2(3) = 16-bit encoding */
/* where o0 = op0 - 2.  Used in MRS/MSR as bits [20:5]. */
#define SYSREG_SCTLR_EL1    0x4080  /* S3_0_C1_C0_0 */
#define SYSREG_TTBR0_EL1    0x4100  /* S3_0_C2_C0_0 */
#define SYSREG_TTBR1_EL1    0x4101  /* S3_0_C2_C0_1 */
#define SYSREG_TCR_EL1      0x4102  /* S3_0_C2_C0_2 */
#define SYSREG_MAIR_EL1     0x4510  /* S3_0_C10_C2_0 */
#define SYSREG_VBAR_EL1     0x4600  /* S3_0_C12_C0_0 */
#define SYSREG_CURRENTEL    0x4212  /* S3_0_C4_C2_2 */
#define SYSREG_DAIF         0x5A11  /* S3_3_C4_C2_1 */
#define SYSREG_NZCV         0x5A10  /* S3_3_C4_C2_0 */
#define SYSREG_FPCR         0x5A20  /* S3_3_C4_C4_0 */
#define SYSREG_FPSR         0x5A21  /* S3_3_C4_C4_1 */
#define SYSREG_TPIDR_EL0    0x5E82  /* S3_3_C13_C0_2 */
#define SYSREG_TPIDR_EL1    0x4684  /* S3_0_C13_C0_4 */
#define SYSREG_CNTFRQ_EL0   0x5F00  /* S3_3_C14_C0_0 */
#define SYSREG_CNTVCT_EL0   0x5F02  /* S3_3_C14_C0_2 */
#define SYSREG_SP_EL0       0x4208  /* S3_0_C4_C1_0 */
#define SYSREG_ELR_EL1      0x4201  /* S3_0_C4_C0_1 */
#define SYSREG_SPSR_EL1     0x4200  /* S3_0_C4_C0_0 */
/* Additional kernel-critical system registers */
/* Encoding: o0(2):op1(3):CRn(4):CRm(4):op2(3) where o0 = op0-2 */
#define SYSREG_ESR_EL1      0x4290  /* 1_000_0101_0010_000 = S3_0_C5_C2_0 */
#define SYSREG_FAR_EL1      0x4300  /* 1_000_0110_0000_000 = S3_0_C6_C0_0 */
#define SYSREG_CPACR_EL1    0x4082  /* 1_000_0001_0000_010 = S3_0_C1_C0_2 */
#define SYSREG_PAR_EL1      0x43A0  /* 1_000_0111_0100_000 = S3_0_C7_C4_0 */
#define SYSREG_CONTEXTIDR_EL1 0x4681 /* 1_000_1101_0000_001 = S3_0_C13_C0_1 */
#define SYSREG_AMAIR_EL1    0x4518  /* 1_000_1010_0011_000 = S3_0_C10_C3_0 */
#define SYSREG_CNTKCTL_EL1  0x4708  /* 1_000_1110_0001_000 = S3_0_C14_C1_0 */

/* System register instructions */
u32 a64_mrs(int rt, u32 sysreg);             /* MRS Xt, sysreg */
u32 a64_msr(u32 sysreg, int rt);             /* MSR sysreg, Xt */
u32 a64_msr_imm(int field, int imm4);        /* MSR pstate, #imm */

/* Exception/System */
u32 a64_eret(void);                           /* ERET */
u32 a64_hvc(u32 imm16);                      /* HVC #imm16 */
u32 a64_smc(u32 imm16);                      /* SMC #imm16 */
u32 a64_wfe(void);                            /* WFE */
u32 a64_wfi(void);                            /* WFI */
u32 a64_yield(void);                          /* YIELD */
u32 a64_sev(void);                            /* SEV */
u32 a64_sevl(void);                           /* SEVL */

/* Conditional select */
u32 a64_csel(int rd, int rn, int rm, int cond);    /* CSEL Xd, Xn, Xm, cond */
u32 a64_csel_w(int rd, int rn, int rm, int cond);  /* CSEL Wd, Wn, Wm, cond */
u32 a64_csinc(int rd, int rn, int rm, int cond);   /* CSINC Xd, Xn, Xm, cond */
u32 a64_csinc_w(int rd, int rn, int rm, int cond); /* CSINC Wd, Wn, Wm, cond */
u32 a64_csinv(int rd, int rn, int rm, int cond);   /* CSINV Xd, Xn, Xm, cond */
u32 a64_csinv_w(int rd, int rn, int rm, int cond); /* CSINV Wd, Wn, Wm, cond */
u32 a64_csneg(int rd, int rn, int rm, int cond);   /* CSNEG Xd, Xn, Xm, cond */
u32 a64_csneg_w(int rd, int rn, int rm, int cond); /* CSNEG Wd, Wn, Wm, cond */

/* Test and branch */
u32 a64_tbnz(int rt, int bit, i32 off14);    /* TBNZ Xt, #bit, label */
u32 a64_tbz(int rt, int bit, i32 off14);     /* TBZ Xt, #bit, label */

/* Bit manipulation */
u32 a64_clz(int rd, int rn);                 /* CLZ Xd, Xn */
u32 a64_clz_w(int rd, int rn);              /* CLZ Wd, Wn */
u32 a64_rbit(int rd, int rn);                /* RBIT Xd, Xn */
u32 a64_rbit_w(int rd, int rn);             /* RBIT Wd, Wn */
u32 a64_rev(int rd, int rn);                 /* REV Xd, Xn */
u32 a64_rev_w(int rd, int rn);              /* REV Wd, Wn */
u32 a64_rev16(int rd, int rn);               /* REV16 Xd, Xn */
u32 a64_rev16_w(int rd, int rn);            /* REV16 Wd, Wn */
u32 a64_rev32(int rd, int rn);               /* REV32 Xd, Xn */

/* Bitfield */
u32 a64_ubfm(int rd, int rn, int immr, int imms); /* UBFM Xd, Xn, #r, #s */
u32 a64_ubfm_w(int rd, int rn, int immr, int imms); /* UBFM Wd, Wn, #r, #s */
u32 a64_sbfm(int rd, int rn, int immr, int imms); /* SBFM Xd, Xn, #r, #s */
u32 a64_sbfm_w(int rd, int rn, int immr, int imms); /* SBFM Wd, Wn, #r, #s */
u32 a64_bfm(int rd, int rn, int immr, int imms);  /* BFM Xd, Xn, #r, #s */
u32 a64_bfm_w(int rd, int rn, int immr, int imms); /* BFM Wd, Wn, #r, #s */

/* Logical immediate */
u32 a64_and_i(int rd, int rn, u64 bitmask);  /* AND Xd, Xn, #bitmask */
u32 a64_orr_i(int rd, int rn, u64 bitmask);  /* ORR Xd, Xn, #bitmask */
u32 a64_eor_i(int rd, int rn, u64 bitmask);  /* EOR Xd, Xn, #bitmask */

/* Pre/post-index addressing */
u32 a64_ldr_pre(int rt, int rn, i32 off9);   /* LDR Xt, [Xn, #off]! */
u32 a64_str_post(int rt, int rn, i32 off9);  /* STR Xt, [Xn], #off */
u32 a64_ldr_r(int rt, int rn, int rm);       /* LDR Xt, [Xn, Xm] */
u32 a64_str_pre(int rt, int rn, i32 off9);   /* STR Xt, [Xn, #off]! */
u32 a64_ldr_post(int rt, int rn, i32 off9);  /* LDR Xt, [Xn], #off */
u32 a64_str_r(int rt, int rn, int rm);       /* STR Xt, [Xn, Xm] */

/* FP double pre/post-index addressing */
u32 a64_ldr_d_pre(int rt, int rn, i32 off9);  /* LDR Dt, [Xn, #off]! */
u32 a64_str_d_pre(int rt, int rn, i32 off9);  /* STR Dt, [Xn, #off]! */
u32 a64_ldr_d_post(int rt, int rn, i32 off9); /* LDR Dt, [Xn], #off */
u32 a64_str_d_post(int rt, int rn, i32 off9); /* STR Dt, [Xn], #off */

/* BTI/PAC */
u32 a64_bti(int variant);                    /* BTI c/j/jc (0/1/2) */
u32 a64_paciasp(void);                       /* PACIASP */
u32 a64_autiasp(void);                       /* AUTIASP */

/* System instructions (SYS aliases: dc, ic, tlbi, at) */
u32 a64_sys(int op1, int crn, int crm, int op2, int rt);
u32 a64_hint(int imm);                          /* HINT #imm */
u32 a64_clrex(void);                            /* CLREX */

/* Load-acquire / Store-release (standalone) */
u32 a64_ldar(int rt, int rn);                   /* LDAR Xt, [Xn] */
u32 a64_ldar_w(int rt, int rn);                 /* LDAR Wt, [Xn] */
u32 a64_ldarb(int rt, int rn);                  /* LDARB Wt, [Xn] */
u32 a64_ldarh(int rt, int rn);                  /* LDARH Wt, [Xn] */
u32 a64_stlr(int rt, int rn);                   /* STLR Xt, [Xn] */
u32 a64_stlr_w(int rt, int rn);                 /* STLR Wt, [Xn] */
u32 a64_stlrb(int rt, int rn);                  /* STLRB Wt, [Xn] */
u32 a64_stlrh(int rt, int rn);                  /* STLRH Wt, [Xn] */

/* Load-acquire RCpc */
u32 a64_ldapr(int rt, int rn);                  /* LDAPR Xt, [Xn] */
u32 a64_ldapr_w(int rt, int rn);                /* LDAPR Wt, [Xn] */
u32 a64_ldaprb(int rt, int rn);                 /* LDAPRB Wt, [Xn] */
u32 a64_ldaprh(int rt, int rn);                 /* LDAPRH Wt, [Xn] */

/* Conditional compare */
u32 a64_ccmp_i(int rn, int imm5, int nzcv, int cond);    /* CCMP Xn, #imm, #nzcv, cond */
u32 a64_ccmp_r(int rn, int rm, int nzcv, int cond);      /* CCMP Xn, Xm, #nzcv, cond */
u32 a64_ccmp_i_w(int rn, int imm5, int nzcv, int cond);  /* CCMP Wn, #imm, #nzcv, cond */
u32 a64_ccmp_r_w(int rn, int rm, int nzcv, int cond);    /* CCMP Wn, Wm, #nzcv, cond */
u32 a64_ccmn_i(int rn, int imm5, int nzcv, int cond);    /* CCMN Xn, #imm, #nzcv, cond */
u32 a64_ccmn_r(int rn, int rm, int nzcv, int cond);      /* CCMN Xn, Xm, #nzcv, cond */
u32 a64_ccmn_i_w(int rn, int imm5, int nzcv, int cond);  /* CCMN Wn, #imm, #nzcv, cond */
u32 a64_ccmn_r_w(int rn, int rm, int nzcv, int cond);    /* CCMN Wn, Wm, #nzcv, cond */

/* Extract */
u32 a64_extr(int rd, int rn, int rm, int lsb);    /* EXTR Xd, Xn, Xm, #lsb */
u32 a64_extr_w(int rd, int rn, int rm, int lsb);  /* EXTR Wd, Wn, Wm, #lsb */

/* Prefetch */
u32 a64_prfm(int type, int rn, i32 offset);     /* PRFM type, [Xn, #off] */

/* Signed byte/halfword load to 64-bit */
u32 a64_ldrsb(int rt, int rn, i32 offset);      /* LDRSB Xt, [Xn, #off] */
u32 a64_ldrsh(int rt, int rn, i32 offset);      /* LDRSH Xt, [Xn, #off] */

/* Multiply-accumulate */
u32 a64_madd(int rd, int rn, int rm, int ra);   /* MADD Xd, Xn, Xm, Xa */
u32 a64_smaddl(int rd, int rn, int rm, int ra); /* SMADDL Xd, Wn, Wm, Xa */
u32 a64_umaddl(int rd, int rn, int rm, int ra); /* UMADDL Xd, Wn, Wm, Xa */

/* Logical - additional register forms */
u32 a64_orn_r(int rd, int rn, int rm);          /* ORN Xd, Xn, Xm */
u32 a64_ands_r(int rd, int rn, int rm);         /* ANDS Xd, Xn, Xm (sets flags) */
u32 a64_bics_r(int rd, int rn, int rm);         /* BICS Xd, Xn, Xm (sets flags) */
u32 a64_ands_i(int rd, int rn, u64 bitmask);    /* ANDS Xd, Xn, #bitmask */
#define a64_tst_r(rn, rm) a64_ands_r(REG_XZR, rn, rm)
#define a64_tst_i(rn, imm) a64_ands_i(REG_XZR, rn, imm)

/* Arithmetic with flags */
u32 a64_adds_r(int rd, int rn, int rm);         /* ADDS Xd, Xn, Xm */
u32 a64_adds_i(int rd, int rn, u32 imm12);      /* ADDS Xd, Xn, #imm12 */

/* Non-temporal pair load/store */
u32 a64_stnp(int rt1, int rt2, int rn, i32 offset);  /* STNP Xt1, Xt2, [Xn, #off] */
u32 a64_ldnp(int rt1, int rt2, int rn, i32 offset);  /* LDNP Xt1, Xt2, [Xn, #off] */

/* System */
#define a64_svc(imm) ((u32)(0xD4000001 | ((imm) << 5)))
#define a64_nop()    ((u32)0xD503201F)

/* ---- Floating-point instructions ---- */

/* FP arithmetic (double-precision, scalar) */
u32 a64_fadd_d(int rd, int rn, int rm);    /* FADD Dd, Dn, Dm */
u32 a64_fsub_d(int rd, int rn, int rm);    /* FSUB Dd, Dn, Dm */
u32 a64_fmul_d(int rd, int rn, int rm);    /* FMUL Dd, Dn, Dm */
u32 a64_fdiv_d(int rd, int rn, int rm);    /* FDIV Dd, Dn, Dm */
u32 a64_fneg_d(int rd, int rn);            /* FNEG Dd, Dn */

/* FP arithmetic (single-precision, scalar) */
u32 a64_fadd_s(int rd, int rn, int rm);    /* FADD Sd, Sn, Sm */
u32 a64_fsub_s(int rd, int rn, int rm);    /* FSUB Sd, Sn, Sm */
u32 a64_fmul_s(int rd, int rn, int rm);    /* FMUL Sd, Sn, Sm */
u32 a64_fdiv_s(int rd, int rn, int rm);    /* FDIV Sd, Sn, Sm */
u32 a64_fneg_s(int rd, int rn);            /* FNEG Sd, Sn */

/* FP compare */
u32 a64_fcmp_d(int rn, int rm);            /* FCMP Dn, Dm */
u32 a64_fcmp_s(int rn, int rm);            /* FCMP Sn, Sm */

/* FP move */
u32 a64_fmov_d(int rd, int rn);            /* FMOV Dd, Dn */
u32 a64_fmov_s(int rd, int rn);            /* FMOV Sd, Sn */
u32 a64_fmov_d_x(int rd, int rn);          /* FMOV Dd, Xn (GPR->FPR) */
u32 a64_fmov_x_d(int rd, int rn);          /* FMOV Xd, Dn (FPR->GPR) */

/* FP convert */
u32 a64_fcvt_ds(int rd, int rn);           /* FCVT Dd, Sn (single->double) */
u32 a64_fcvt_sd(int rd, int rn);           /* FCVT Sd, Dn (double->single) */
u32 a64_scvtf_d(int rd, int rn);           /* SCVTF Dd, Xn (signed int->double) */
u32 a64_scvtf_s(int rd, int rn);           /* SCVTF Sd, Wn (signed int->float) */
u32 a64_ucvtf_d(int rd, int rn);           /* UCVTF Dd, Xn (unsigned int->double) */
u32 a64_ucvtf_s(int rd, int rn);           /* UCVTF Sd, Wn (unsigned int->float) */
u32 a64_fcvtzs_xd(int rd, int rn);         /* FCVTZS Xd, Dn (double->signed long) */
u32 a64_fcvtzs_ws(int rd, int rn);         /* FCVTZS Wd, Sn (float->signed int) */

/* FP load/store (64-bit double) */
u32 a64_ldr_d(int rt, int rn, i32 offset); /* LDR Dt, [Xn, #off] */
u32 a64_str_d(int rt, int rn, i32 offset); /* STR Dt, [Xn, #off] */

/* FP load/store (32-bit float) */
u32 a64_ldr_s(int rt, int rn, i32 offset); /* LDR St, [Xn, #off] */
u32 a64_str_s(int rt, int rn, i32 offset); /* STR St, [Xn, #off] */

#endif /* FREE_AARCH64_H */
