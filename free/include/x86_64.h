/*
 * x86_64.h - x86_64 instruction encoding for the free toolchain
 * Variable-length instruction encoding with REX, ModR/M, SIB, displacement.
 * Pure C89. No external dependencies.
 */
#ifndef FREE_X86_64_H
#define FREE_X86_64_H

#include "free.h"

/* ---- Register numbers (encoding values) ---- */
/* Low 8 registers (no REX.B needed) */
#define X86_RAX  0
#define X86_RCX  1
#define X86_RDX  2
#define X86_RBX  3
#define X86_RSP  4
#define X86_RBP  5
#define X86_RSI  6
#define X86_RDI  7
/* Extended registers (need REX.B or REX.R) */
#define X86_R8   8
#define X86_R9   9
#define X86_R10  10
#define X86_R11  11
#define X86_R12  12
#define X86_R13  13
#define X86_R14  14
#define X86_R15  15

/* ---- System V AMD64 ABI: argument registers ---- */
/* args: rdi, rsi, rdx, rcx, r8, r9 */
/* return: rax */
/* callee-saved: rbx, rbp, r12-r15 */
/* caller-saved: rax, rcx, rdx, rsi, rdi, r8-r11 */

/* ---- Condition codes for Jcc and SETcc ---- */
#define X86_CC_O   0x0   /* overflow */
#define X86_CC_NO  0x1   /* not overflow */
#define X86_CC_B   0x2   /* below (unsigned <) / carry */
#define X86_CC_AE  0x3   /* above or equal (unsigned >=) */
#define X86_CC_E   0x4   /* equal / zero */
#define X86_CC_NE  0x5   /* not equal / not zero */
#define X86_CC_BE  0x6   /* below or equal (unsigned <=) */
#define X86_CC_A   0x7   /* above (unsigned >) */
#define X86_CC_S   0x8   /* sign (negative) */
#define X86_CC_NS  0x9   /* not sign */
#define X86_CC_L   0xC   /* less (signed <) */
#define X86_CC_GE  0xD   /* greater or equal (signed >=) */
#define X86_CC_LE  0xE   /* less or equal (signed <=) */
#define X86_CC_G   0xF   /* greater (signed >) */

/* ---- Linux x86_64 syscall numbers ---- */
#define X86_SYS_READ        0
#define X86_SYS_WRITE       1
#define X86_SYS_OPEN        2
#define X86_SYS_CLOSE       3
#define X86_SYS_LSEEK       8
#define X86_SYS_MMAP        9
#define X86_SYS_MUNMAP      11
#define X86_SYS_BRK         12
#define X86_SYS_EXIT_GROUP  231

/* ---- x86_64 ELF constants ---- */
#define EM_X86_64  62

/* x86_64 relocations */
#define R_X86_64_NONE      0
#define R_X86_64_64        1
#define R_X86_64_PC32      2
#define R_X86_64_32        10
#define R_X86_64_PLT32     4

/* ---- REX prefix ---- */
/*
 * REX byte format: 0100 WRXB
 * W = 64-bit operand size
 * R = ModR/M reg field extension
 * X = SIB index field extension
 * B = ModR/M r/m field or SIB base extension
 */
#define REX_BASE  0x40
#define REX_W     0x08   /* 64-bit operand */
#define REX_R     0x04   /* extend ModR/M reg */
#define REX_X     0x02   /* extend SIB index */
#define REX_B     0x01   /* extend ModR/M r/m or SIB base */

/* ---- ModR/M byte ---- */
/*
 * ModR/M format: mod(2) reg(3) r/m(3)
 * mod=00: [r/m]            (indirect)
 * mod=01: [r/m + disp8]
 * mod=10: [r/m + disp32]
 * mod=11: r/m (register)
 */
#define MODRM(mod, reg, rm) \
    (u8)(((mod) << 6) | (((reg) & 7) << 3) | ((rm) & 7))

/* ---- Instruction encoding buffer ---- */
/*
 * x86_64 instructions are variable-length (1-15 bytes).
 * We encode into a buffer and return the length.
 */
#define X86_MAX_INSN  15

/*
 * x86_buf - buffer for encoding a single instruction.
 * After calling an encoder, buf[0..len-1] contains the bytes.
 */
struct x86_buf {
    u8 buf[X86_MAX_INSN];
    int len;
};

/* ---- Encoding helpers ---- */

/*
 * x86_rex - compute REX prefix for given register operands.
 * w: 1 for 64-bit operand size
 * reg: ModR/M reg field register number (0-15)
 * rm: ModR/M r/m field register number (0-15)
 * Returns 0 if no REX needed, otherwise REX byte.
 */
u8 x86_rex(int w, int reg, int rm);

/*
 * x86_rex_sib - compute REX prefix with SIB index.
 */
u8 x86_rex_sib(int w, int reg, int idx, int base);

/* ---- Instruction encoders ---- */
/* All return the number of bytes written to buf. */

/* Move register to register: MOV r64, r64 */
int x86_mov_rr(u8 *buf, int dst, int src);

/* Move immediate to register: MOV r64, imm64 (or shorter forms) */
int x86_mov_ri(u8 *buf, int dst, long imm);

/* Move 32-bit immediate to register: MOV r32, imm32 (zero-extended) */
int x86_mov_ri32(u8 *buf, int dst, i32 imm);

/* Load from [base + disp]: MOV r64, [base + disp32] */
int x86_mov_rm(u8 *buf, int dst, int base, i32 disp);

/* Store to [base + disp]: MOV [base + disp32], r64 */
int x86_mov_mr(u8 *buf, int base, i32 disp, int src);

/* Load byte: MOVZX r64, byte [base + disp] */
int x86_movzx_rm8(u8 *buf, int dst, int base, i32 disp);

/* Load byte sign-extended: MOVSX r64, byte [base + disp] */
int x86_movsx_rm8(u8 *buf, int dst, int base, i32 disp);

/* Store byte: MOV byte [base + disp], r8 */
int x86_mov_mr8(u8 *buf, int base, i32 disp, int src);

/* Load 32-bit: MOV r32, [base + disp32] */
int x86_mov_rm32(u8 *buf, int dst, int base, i32 disp);

/* Store 32-bit: MOV [base + disp32], r32 */
int x86_mov_mr32(u8 *buf, int base, i32 disp, int src);

/* Load 32-bit sign-extended: MOVSXD r64, [base + disp32] */
int x86_movsxd_rm(u8 *buf, int dst, int base, i32 disp);

/* Push register */
int x86_push_r(u8 *buf, int reg);

/* Pop register */
int x86_pop_r(u8 *buf, int reg);

/* ADD r64, r64 */
int x86_add_rr(u8 *buf, int dst, int src);

/* ADD r64, imm32 */
int x86_add_ri(u8 *buf, int dst, i32 imm);

/* SUB r64, r64 */
int x86_sub_rr(u8 *buf, int dst, int src);

/* SUB r64, imm32 */
int x86_sub_ri(u8 *buf, int dst, i32 imm);

/* IMUL r64, r64 */
int x86_imul_rr(u8 *buf, int dst, int src);

/* CQO (sign-extend rax into rdx:rax) */
int x86_cqo(u8 *buf);

/* IDIV r64 (signed divide rdx:rax by r64) */
int x86_idiv_r(u8 *buf, int src);

/* DIV r64 (unsigned divide rdx:rax by r64) */
int x86_div_r(u8 *buf, int src);

/* NEG r64 */
int x86_neg_r(u8 *buf, int reg);

/* NOT r64 */
int x86_not_r(u8 *buf, int reg);

/* AND r64, r64 */
int x86_and_rr(u8 *buf, int dst, int src);

/* OR r64, r64 */
int x86_or_rr(u8 *buf, int dst, int src);

/* XOR r64, r64 */
int x86_xor_rr(u8 *buf, int dst, int src);

/* XOR r32, r32 (for zeroing) */
int x86_xor_rr32(u8 *buf, int dst, int src);

/* SHL r64, cl */
int x86_shl_cl(u8 *buf, int dst);

/* SHR r64, cl */
int x86_shr_cl(u8 *buf, int dst);

/* SAR r64, cl */
int x86_sar_cl(u8 *buf, int dst);

/* CMP r64, r64 */
int x86_cmp_rr(u8 *buf, int a, int b);

/* CMP r64, imm32 */
int x86_cmp_ri(u8 *buf, int reg, i32 imm);

/* TEST r64, r64 */
int x86_test_rr(u8 *buf, int a, int b);

/* SETcc r8 (byte register) */
int x86_setcc(u8 *buf, int cc, int dst);

/* MOVZX r64, r8 (zero-extend byte to 64-bit) */
int x86_movzx_rr8(u8 *buf, int dst, int src);

/* JMP rel32 (returns instruction length; caller fills offset) */
int x86_jmp_rel32(u8 *buf, i32 offset);

/* Jcc rel32 (returns instruction length; caller fills offset) */
int x86_jcc_rel32(u8 *buf, int cc, i32 offset);

/* CALL rel32 */
int x86_call_rel32(u8 *buf, i32 offset);

/* CALL r64 (indirect) */
int x86_call_r(u8 *buf, int reg);

/* RET */
int x86_ret(u8 *buf);

/* LEA r64, [base + disp32] */
int x86_lea(u8 *buf, int dst, int base, i32 disp);

/* LEA r64, [rip + disp32] (RIP-relative) */
int x86_lea_rip(u8 *buf, int dst, i32 disp);

/* NOP */
int x86_nop(u8 *buf);

/* SYSCALL */
int x86_syscall(u8 *buf);

/* AND r64, imm32 */
int x86_and_ri(u8 *buf, int dst, i32 imm);

/* MOVSX r64, r8 (sign-extend byte) */
int x86_movsx_rr8(u8 *buf, int dst, int src);

/* MOVSX r64, r16 (sign-extend word) */
int x86_movsx_rr16(u8 *buf, int dst, int src);

/* MOVSXD r64, r32 (sign-extend dword) */
int x86_movsxd_rr(u8 *buf, int dst, int src);

/* MOV r32, r32 (zero-extends to 64) */
int x86_mov_rr32(u8 *buf, int dst, int src);

#endif /* FREE_X86_64_H */
