/*
 * test_reloc.c - Tests for AArch64 relocation patching.
 * Each test constructs mock instruction bytes, applies a relocation
 * algorithm, and verifies the result matches the expected encoding.
 * Pure C89.
 */

#include "../test.h"
#include <string.h>

#include "free.h"
#include "elf.h"

/* ===== little-endian read/write helpers ===== */

static u32 read32le(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) |
           ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void write32le(u8 *p, u32 v)
{
    p[0] = (u8)(v & 0xff);
    p[1] = (u8)((v >> 8) & 0xff);
    p[2] = (u8)((v >> 16) & 0xff);
    p[3] = (u8)((v >> 24) & 0xff);
}

static u16 read16le(const u8 *p)
{
    return (u16)p[0] | ((u16)p[1] << 8);
}

static void write16le(u8 *p, u16 v)
{
    p[0] = (u8)(v & 0xff);
    p[1] = (u8)((v >> 8) & 0xff);
}

static u64 read64le(const u8 *p)
{
    return (u64)read32le(p) | ((u64)read32le(p + 4) << 32);
}

static void write64le(u8 *p, u64 v)
{
    write32le(p, (u32)(v & 0xffffffffUL));
    write32le(p + 4, (u32)(v >> 32));
}

/*
 * apply_reloc: mirrors the linker's apply_one_reloc logic.
 *
 *   data       - pointer to the instruction/data bytes to patch
 *   patch_addr - virtual address of the patch site
 *   rtype      - relocation type (R_AARCH64_*)
 *   sym_addr   - resolved symbol virtual address
 *   addend     - addend from the Elf64_Rela entry
 */
static void apply_reloc(u8 *data, u64 patch_addr,
                         u32 rtype, u64 sym_addr, i64 addend)
{
    u32 insn;
    i64 val;
    u64 page_s;
    u64 page_p;

    switch (rtype) {
    case R_AARCH64_NONE:
        break;

    case R_AARCH64_ABS64:
        write64le(data, sym_addr + (u64)addend);
        break;

    case R_AARCH64_ABS32:
        write32le(data, (u32)(sym_addr + (u64)addend));
        break;

    case R_AARCH64_PREL32:
        val = (i64)(sym_addr + (u64)addend) - (i64)patch_addr;
        write32le(data, (u32)val);
        break;

    case R_AARCH64_PREL16:
        val = (i64)(sym_addr + (u64)addend) - (i64)patch_addr;
        write16le(data, (u16)val);
        break;

    case R_AARCH64_MOVW_UABS_G0:
    case R_AARCH64_MOVW_UABS_G0_NC:
        val = (i64)(sym_addr + (u64)addend) & 0xffff;
        insn = read32le(data);
        insn &= ~(u32)(0xffff << 5);
        insn |= ((u32)val & 0xffff) << 5;
        write32le(data, insn);
        break;

    case R_AARCH64_MOVW_UABS_G1:
    case R_AARCH64_MOVW_UABS_G1_NC:
        val = (i64)((sym_addr + (u64)addend) >> 16) & 0xffff;
        insn = read32le(data);
        insn &= ~(u32)(0xffff << 5);
        insn |= ((u32)val & 0xffff) << 5;
        write32le(data, insn);
        break;

    case R_AARCH64_MOVW_UABS_G2:
    case R_AARCH64_MOVW_UABS_G2_NC:
        val = (i64)((sym_addr + (u64)addend) >> 32) & 0xffff;
        insn = read32le(data);
        insn &= ~(u32)(0xffff << 5);
        insn |= ((u32)val & 0xffff) << 5;
        write32le(data, insn);
        break;

    case R_AARCH64_MOVW_UABS_G3:
        val = (i64)((sym_addr + (u64)addend) >> 48) & 0xffff;
        insn = read32le(data);
        insn &= ~(u32)(0xffff << 5);
        insn |= ((u32)val & 0xffff) << 5;
        write32le(data, insn);
        break;

    case R_AARCH64_ADR_PREL_LO21:
        val = (i64)(sym_addr + (u64)addend) - (i64)patch_addr;
        insn = read32le(data);
        insn &= 0x9f00001fUL;
        insn |= ((u32)val & 0x3) << 29;
        insn |= (((u32)val >> 2) & 0x7ffff) << 5;
        write32le(data, insn);
        break;

    case R_AARCH64_ADR_PREL_PG_HI21:
        page_s = (sym_addr + (u64)addend) & ~(u64)0xfff;
        page_p = patch_addr & ~(u64)0xfff;
        val = (i64)(page_s - page_p);
        val >>= 12;
        insn = read32le(data);
        insn &= 0x9f00001fUL;
        insn |= ((u32)val & 0x3) << 29;          /* immlo */
        insn |= (((u32)val >> 2) & 0x7ffff) << 5; /* immhi */
        write32le(data, insn);
        break;

    case R_AARCH64_ADD_ABS_LO12_NC:
        val = (i64)((sym_addr + (u64)addend) & 0xfff);
        insn = read32le(data);
        insn &= ~(u32)(0xfff << 10);
        insn |= ((u32)val & 0xfff) << 10;
        write32le(data, insn);
        break;

    case R_AARCH64_LDST8_ABS_LO12_NC:
        val = (i64)((sym_addr + (u64)addend) & 0xfff);
        insn = read32le(data);
        insn &= ~(u32)(0xfff << 10);
        insn |= ((u32)val & 0xfff) << 10;
        write32le(data, insn);
        break;

    case R_AARCH64_TSTBR14:
        val = (i64)(sym_addr + (u64)addend) - (i64)patch_addr;
        val >>= 2;
        insn = read32le(data);
        insn &= ~(u32)(0x3fff << 5);
        insn |= ((u32)val & 0x3fff) << 5;
        write32le(data, insn);
        break;

    case R_AARCH64_CONDBR19:
        val = (i64)(sym_addr + (u64)addend) - (i64)patch_addr;
        val >>= 2;
        insn = read32le(data);
        insn &= ~(u32)(0x7ffff << 5);
        insn |= ((u32)val & 0x7ffff) << 5;
        write32le(data, insn);
        break;

    case R_AARCH64_CALL26:
    case R_AARCH64_JUMP26:
        val = (i64)(sym_addr + (u64)addend) - (i64)patch_addr;
        val >>= 2;
        insn = read32le(data);
        insn = (insn & 0xfc000000UL) | ((u32)val & 0x03ffffffUL);
        write32le(data, insn);
        break;

    case R_AARCH64_LDST16_ABS_LO12_NC:
        val = (i64)((sym_addr + (u64)addend) & 0xfff);
        val >>= 1;
        insn = read32le(data);
        insn &= ~(u32)(0xfff << 10);
        insn |= ((u32)val & 0xfff) << 10;
        write32le(data, insn);
        break;

    case R_AARCH64_LDST32_ABS_LO12_NC:
        val = (i64)((sym_addr + (u64)addend) & 0xfff);
        val >>= 2;
        insn = read32le(data);
        insn &= ~(u32)(0xfff << 10);
        insn |= ((u32)val & 0xfff) << 10;
        write32le(data, insn);
        break;

    case R_AARCH64_LDST64_ABS_LO12_NC:
        val = (i64)((sym_addr + (u64)addend) & 0xfff);
        val >>= 3;
        insn = read32le(data);
        insn &= ~(u32)(0xfff << 10);
        insn |= ((u32)val & 0xfff) << 10;
        write32le(data, insn);
        break;

    case R_AARCH64_LDST128_ABS_LO12_NC:
        val = (i64)((sym_addr + (u64)addend) & 0xfff);
        val >>= 4;
        insn = read32le(data);
        insn &= ~(u32)(0xfff << 10);
        insn |= ((u32)val & 0xfff) << 10;
        write32le(data, insn);
        break;

    default:
        break;
    }
}

/* ===== R_AARCH64_CALL26 (BL) tests ===== */

TEST(call26_forward_small)
{
    /*
     * BL to a target 0x100 bytes ahead.
     *   patch_addr = 0x400000
     *   sym_addr   = 0x400100
     *   offset in words = (0x100) / 4 = 0x40
     *
     * BL opcode: 0x94000000 | imm26
     * Expected: 0x94000040
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x94000000); /* BL template */
    apply_reloc(insn, 0x400000, R_AARCH64_CALL26, 0x400100, 0);
    result = read32le(insn);
    ASSERT_EQ(result, 0x94000040);
}

TEST(call26_forward_large)
{
    /*
     * BL forward by 0x1000 bytes.
     *   patch_addr = 0x400000
     *   sym_addr   = 0x401000
     *   offset in words = 0x1000 / 4 = 0x400
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x94000000);
    apply_reloc(insn, 0x400000, R_AARCH64_CALL26, 0x401000, 0);
    result = read32le(insn);
    ASSERT_EQ(result, 0x94000400);
}

TEST(call26_backward)
{
    /*
     * BL backward by 0x100 bytes.
     *   patch_addr = 0x400100
     *   sym_addr   = 0x400000
     *   offset = -0x100, in words = -0x40
     *   imm26 = -0x40 & 0x03ffffff = 0x03ffffc0
     *
     * Expected: 0x94000000 | 0x03ffffc0 = 0x97ffffc0
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x94000000);
    apply_reloc(insn, 0x400100, R_AARCH64_CALL26, 0x400000, 0);
    result = read32le(insn);
    ASSERT_EQ(result, 0x97ffffc0);
}

TEST(call26_with_addend)
{
    /*
     * BL with addend = 8 (skip 2 instructions at target).
     *   patch_addr = 0x400000
     *   sym_addr   = 0x400100
     *   effective offset = 0x100 + 8 = 0x108
     *   in words = 0x108 / 4 = 0x42
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x94000000);
    apply_reloc(insn, 0x400000, R_AARCH64_CALL26, 0x400100, 8);
    result = read32le(insn);
    ASSERT_EQ(result, 0x94000042);
}

TEST(call26_preserves_opcode)
{
    /*
     * The top 6 bits (opcode) must be preserved.
     * BL opcode is 100101 = 0x94000000 >> 26 = 0x25.
     * After relocation the top 6 bits must still be BL.
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x94000000);
    apply_reloc(insn, 0x400000, R_AARCH64_CALL26, 0x400004, 0);
    result = read32le(insn);
    ASSERT_EQ(result & 0xfc000000UL, 0x94000000);
}

TEST(call26_self_reference)
{
    /*
     * BL to self (infinite loop): offset = 0.
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x94000000);
    apply_reloc(insn, 0x400000, R_AARCH64_CALL26, 0x400000, 0);
    result = read32le(insn);
    ASSERT_EQ(result, 0x94000000);
}

/* ===== R_AARCH64_JUMP26 (B) tests ===== */

TEST(jump26_forward)
{
    /*
     * B forward by 0x200 bytes.
     *   B opcode: 0x14000000
     *   offset in words = 0x200 / 4 = 0x80
     * Expected: 0x14000080
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x14000000);
    apply_reloc(insn, 0x400000, R_AARCH64_JUMP26, 0x400200, 0);
    result = read32le(insn);
    ASSERT_EQ(result, 0x14000080);
}

TEST(jump26_backward)
{
    /*
     * B backward by 0x200 bytes.
     *   offset = -0x200, in words = -0x80
     *   imm26 = -0x80 & 0x03ffffff = 0x03ffff80
     * Expected: 0x14000000 | 0x03ffff80 = 0x17ffff80
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x14000000);
    apply_reloc(insn, 0x400200, R_AARCH64_JUMP26, 0x400000, 0);
    result = read32le(insn);
    ASSERT_EQ(result, 0x17ffff80);
}

TEST(jump26_preserves_opcode)
{
    /*
     * B opcode is 000101 = 0x14000000 >> 26 = 0x05.
     * Top 6 bits must remain the B encoding.
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x14000000);
    apply_reloc(insn, 0x400000, R_AARCH64_JUMP26, 0x400100, 0);
    result = read32le(insn);
    ASSERT_EQ(result & 0xfc000000UL, 0x14000000);
}

TEST(jump26_with_addend)
{
    /*
     * B forward by 0x100 + addend 4.
     * offset = 0x104, words = 0x41
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x14000000);
    apply_reloc(insn, 0x400000, R_AARCH64_JUMP26, 0x400100, 4);
    result = read32le(insn);
    ASSERT_EQ(result, 0x14000041);
}

/* ===== R_AARCH64_ABS64 tests ===== */

TEST(abs64_simple)
{
    /*
     * Write a 64-bit absolute address.
     * sym_addr = 0x0000000000400080, addend = 0
     */
    u8 data[8];
    u64 result;

    memset(data, 0, sizeof(data));
    apply_reloc(data, 0, R_AARCH64_ABS64, 0x400080, 0);
    result = read64le(data);
    ASSERT_EQ(result, 0x400080);
}

TEST(abs64_with_addend)
{
    u8 data[8];
    u64 result;

    memset(data, 0, sizeof(data));
    apply_reloc(data, 0, R_AARCH64_ABS64, 0x400000, 0x80);
    result = read64le(data);
    ASSERT_EQ(result, 0x400080);
}

TEST(abs64_negative_addend)
{
    u8 data[8];
    u64 result;

    memset(data, 0, sizeof(data));
    apply_reloc(data, 0, R_AARCH64_ABS64, 0x400080, -0x80);
    result = read64le(data);
    ASSERT_EQ(result, 0x400000);
}

TEST(abs64_high_address)
{
    /*
     * Test a high-memory address to verify all 64 bits are written.
     */
    u8 data[8];
    u64 result;
    u64 addr;

    addr = 0xFFFF000010000000UL;
    memset(data, 0, sizeof(data));
    apply_reloc(data, 0, R_AARCH64_ABS64, addr, 0);
    result = read64le(data);
    ASSERT_EQ(result, addr);
}

TEST(abs64_overwrites_existing)
{
    u8 data[8];
    u64 result;

    memset(data, 0xFF, sizeof(data));
    apply_reloc(data, 0, R_AARCH64_ABS64, 0x1234, 0);
    result = read64le(data);
    ASSERT_EQ(result, 0x1234);
}

/* ===== R_AARCH64_ADR_PREL_PG_HI21 (ADRP) tests ===== */

TEST(adrp_same_page)
{
    /*
     * ADRP where symbol and patch site are on the same 4K page.
     *   patch_addr = 0x400000
     *   sym_addr   = 0x400100
     *   Page(S) = 0x400000, Page(P) = 0x400000
     *   page_delta = 0, val = 0
     *
     * ADRP template: 0x90000000 | rd
     * With val=0, output should be: 0x90000000 | rd
     * Using rd=0 (X0): template = 0x90000000
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x90000000); /* ADRP X0 */
    apply_reloc(insn, 0x400000, R_AARCH64_ADR_PREL_PG_HI21, 0x400100, 0);
    result = read32le(insn);
    /* immhi and immlo are both 0, so only rd (0) and fixed bits remain */
    ASSERT_EQ(result & 0x9f00001fUL, 0x90000000);
    /* full result: same page means delta=0, so entire imm is 0 */
    ASSERT_EQ(result, 0x90000000);
}

TEST(adrp_next_page)
{
    /*
     * ADRP where symbol is on the next page.
     *   patch_addr = 0x400000
     *   sym_addr   = 0x401000
     *   Page(S) = 0x401000, Page(P) = 0x400000
     *   page_delta = 0x1000, val = 0x1000 >> 12 = 1
     *
     * val = 1: immlo = val & 3 = 1, immhi = (val >> 2) & 0x7ffff = 0
     * insn = 0x90000000 | (1 << 29) = 0xb0000000
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x90000000);
    apply_reloc(insn, 0x400000, R_AARCH64_ADR_PREL_PG_HI21, 0x401000, 0);
    result = read32le(insn);
    ASSERT_EQ(result, 0xb0000000);
}

TEST(adrp_four_pages_forward)
{
    /*
     * ADRP with page delta = 4.
     *   patch_addr = 0x400000
     *   sym_addr   = 0x404000
     *   page_delta = 4 pages
     *   val = 4: immlo = 4 & 3 = 0, immhi = (4 >> 2) & 0x7ffff = 1
     *   insn = 0x90000000 | (0 << 29) | (1 << 5) = 0x90000020
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x90000000);
    apply_reloc(insn, 0x400000, R_AARCH64_ADR_PREL_PG_HI21, 0x404000, 0);
    result = read32le(insn);
    ASSERT_EQ(result, 0x90000020);
}

TEST(adrp_preserves_rd)
{
    /*
     * ADRP X5: template = 0x90000005
     * After relocation with same-page delta=0, rd must still be 5.
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x90000005); /* ADRP X5 */
    apply_reloc(insn, 0x400000, R_AARCH64_ADR_PREL_PG_HI21, 0x400800, 0);
    result = read32le(insn);
    ASSERT_EQ(result & 0x1f, 5);
}

TEST(adrp_with_addend)
{
    /*
     * ADRP with addend that pushes onto next page.
     *   patch_addr = 0x400000
     *   sym_addr   = 0x400F00, addend = 0x200
     *   S+A = 0x401100
     *   Page(S+A) = 0x401000, Page(P) = 0x400000
     *   page_delta = 1
     *   Same encoding as next_page test.
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x90000000);
    apply_reloc(insn, 0x400000, R_AARCH64_ADR_PREL_PG_HI21, 0x400F00, 0x200);
    result = read32le(insn);
    ASSERT_EQ(result, 0xb0000000);
}

/* ===== R_AARCH64_ADD_ABS_LO12_NC tests ===== */

TEST(add_lo12_zero)
{
    /*
     * ADD X0, X0, #0 (page-aligned symbol).
     *   sym_addr = 0x401000 (lo12 = 0x000)
     *   ADD template: 0x91000000 (ADD X0, X0, #0)
     *   Expected: imm12 field (bits [21:10]) = 0
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x91000000);
    apply_reloc(insn, 0, R_AARCH64_ADD_ABS_LO12_NC, 0x401000, 0);
    result = read32le(insn);
    ASSERT_EQ(result, 0x91000000);
}

TEST(add_lo12_nonzero)
{
    /*
     * sym_addr = 0x401080, lo12 = 0x080 = 128
     * imm12 field = 128 << 10 = 0x20000
     * Expected: 0x91000000 | 0x00020000 = 0x91020000
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x91000000);
    apply_reloc(insn, 0, R_AARCH64_ADD_ABS_LO12_NC, 0x401080, 0);
    result = read32le(insn);
    ASSERT_EQ(result, 0x91020000);
}

TEST(add_lo12_max)
{
    /*
     * sym_addr with lo12 = 0xFFF (maximum 12-bit value).
     * imm12 field = 0xFFF << 10 = 0x003FFC00
     * Expected: 0x91000000 | 0x003FFC00 = 0x913FFC00
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x91000000);
    apply_reloc(insn, 0, R_AARCH64_ADD_ABS_LO12_NC, 0x400FFF, 0);
    result = read32le(insn);
    ASSERT_EQ(result, 0x913FFC00);
}

TEST(add_lo12_preserves_rd_rn)
{
    /*
     * ADD X1, X2, #imm12
     * Template: 0x91000041 (rd=1, rn=2)
     * sym_addr lo12 = 0x010
     * Expected: rd and rn preserved, imm12 = 0x010 << 10
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x91000041);
    apply_reloc(insn, 0, R_AARCH64_ADD_ABS_LO12_NC, 0x400010, 0);
    result = read32le(insn);
    ASSERT_EQ(result & 0x1f, 1);          /* rd */
    ASSERT_EQ((result >> 5) & 0x1f, 2);   /* rn */
    /* imm12 = 0x010 */
    ASSERT_EQ((result >> 10) & 0xfff, 0x010);
}

TEST(add_lo12_with_addend)
{
    /*
     * sym_addr = 0x401000, addend = 0x20
     * S+A = 0x401020, lo12 = 0x020
     * imm12 field = 0x020 << 10 = 0x8000
     * Expected: 0x91000000 | 0x00008000 = 0x91008000
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x91000000);
    apply_reloc(insn, 0, R_AARCH64_ADD_ABS_LO12_NC, 0x401000, 0x20);
    result = read32le(insn);
    ASSERT_EQ(result, 0x91008000);
}

TEST(add_lo12_clears_old_imm)
{
    /*
     * If the instruction already has a nonzero imm12, it should be
     * replaced, not OR'd into.
     * Start with imm12 = 0xFFF (all bits set), apply lo12 = 0x001.
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x913FFC00); /* ADD X0, X0, #0xFFF */
    apply_reloc(insn, 0, R_AARCH64_ADD_ABS_LO12_NC, 0x400001, 0);
    result = read32le(insn);
    ASSERT_EQ((result >> 10) & 0xfff, 0x001);
}

/* ===== ADRP + ADD pair test ===== */

TEST(adrp_add_pair)
{
    /*
     * Typical pattern: ADRP + ADD to load a full address.
     *   sym_addr   = 0x401234
     *   patch_addr = 0x400000
     *
     * ADRP: Page(0x401234)=0x401000, Page(0x400000)=0x400000
     *        page_delta=1 -> val=1, immlo=1, immhi=0
     *        result: 0x90000000 | (1 << 29) = 0xb0000000
     *
     * ADD:  lo12(0x401234) = 0x234
     *       imm12 = 0x234 << 10
     *       result: 0x91000000 | (0x234 << 10) = 0x9108D000
     */
    u8 adrp[4];
    u8 add[4];
    u32 adrp_result;
    u32 add_result;
    u64 reconstructed;

    /* ADRP X0 */
    write32le(adrp, 0x90000000);
    apply_reloc(adrp, 0x400000, R_AARCH64_ADR_PREL_PG_HI21, 0x401234, 0);
    adrp_result = read32le(adrp);

    /* ADD X0, X0, #lo12 */
    write32le(add, 0x91000000);
    apply_reloc(add, 0, R_AARCH64_ADD_ABS_LO12_NC, 0x401234, 0);
    add_result = read32le(add);

    /*
     * Verify: reconstruct the address from the encoded fields.
     *
     * From ADRP: immlo = (result >> 29) & 3, immhi = (result >> 5) & 0x7ffff
     *   page_offset = (immhi << 2 | immlo) << 12
     *   target_page = page_of(patch_addr) + page_offset
     *
     * From ADD: lo12 = (result >> 10) & 0xfff
     *
     * Full address = target_page + lo12
     */
    {
        u32 immlo;
        u32 immhi;
        i64 page_off;
        u64 target_page;
        u32 lo12;

        immlo = (adrp_result >> 29) & 0x3;
        immhi = (adrp_result >> 5) & 0x7ffff;
        page_off = (i64)((immhi << 2) | immlo) << 12;
        target_page = (u64)((i64)0x400000 + page_off);

        lo12 = (add_result >> 10) & 0xfff;

        reconstructed = target_page + (u64)lo12;
    }

    ASSERT_EQ(reconstructed, 0x401234);
}

/* ===== R_AARCH64_NONE test ===== */

TEST(none_reloc_noop)
{
    u8 data[8];

    memset(data, 0xAA, sizeof(data));
    apply_reloc(data, 0x400000, R_AARCH64_NONE, 0x401000, 0);
    /* data must be unchanged */
    ASSERT_EQ(data[0], 0xAA);
    ASSERT_EQ(data[7], 0xAA);
}

/* ===== R_AARCH64_ABS32 test ===== */

TEST(abs32_simple)
{
    u8 data[4];
    u32 result;

    memset(data, 0, sizeof(data));
    apply_reloc(data, 0, R_AARCH64_ABS32, 0x12345678, 0);
    result = read32le(data);
    ASSERT_EQ(result, 0x12345678);
}

TEST(abs32_with_addend)
{
    u8 data[4];
    u32 result;

    memset(data, 0, sizeof(data));
    apply_reloc(data, 0, R_AARCH64_ABS32, 0x10000, 0x200);
    result = read32le(data);
    ASSERT_EQ(result, 0x10200);
}

/* ===== R_AARCH64_LDST16_ABS_LO12_NC tests ===== */

TEST(ldst16_lo12_aligned)
{
    /*
     * LDRH with halfword-aligned address.
     *   sym_addr = 0x401002, lo12 = 0x002
     *   imm12 = 0x002 >> 1 = 0x001
     *   LDR template: 0x79400000 (LDRH W0, [X0])
     *   Expected: 0x79400000 | (0x001 << 10) = 0x79400400
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x79400000);
    apply_reloc(insn, 0, R_AARCH64_LDST16_ABS_LO12_NC, 0x401002, 0);
    result = read32le(insn);
    ASSERT_EQ((result >> 10) & 0xfff, 0x001);
}

TEST(ldst16_lo12_max)
{
    /*
     * lo12 = 0xFFE (max even value for halfword).
     * imm12 = 0xFFE >> 1 = 0x7FF
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x79400000);
    apply_reloc(insn, 0, R_AARCH64_LDST16_ABS_LO12_NC, 0x400FFE, 0);
    result = read32le(insn);
    ASSERT_EQ((result >> 10) & 0xfff, 0x7FF);
}

TEST(ldst16_lo12_with_addend)
{
    /*
     * sym_addr = 0x401000, addend = 0x004
     * S+A = 0x401004, lo12 = 0x004
     * imm12 = 0x004 >> 1 = 0x002
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x79400000);
    apply_reloc(insn, 0, R_AARCH64_LDST16_ABS_LO12_NC, 0x401000, 0x004);
    result = read32le(insn);
    ASSERT_EQ((result >> 10) & 0xfff, 0x002);
}

/* ===== R_AARCH64_LDST32_ABS_LO12_NC tests ===== */

TEST(ldst32_lo12_aligned)
{
    /*
     * LDR W0, [X0] with word-aligned address.
     *   sym_addr = 0x401004, lo12 = 0x004
     *   imm12 = 0x004 >> 2 = 0x001
     *   LDR template: 0xB9400000
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0xB9400000);
    apply_reloc(insn, 0, R_AARCH64_LDST32_ABS_LO12_NC, 0x401004, 0);
    result = read32le(insn);
    ASSERT_EQ((result >> 10) & 0xfff, 0x001);
}

TEST(ldst32_lo12_max)
{
    /*
     * lo12 = 0xFFC (max word-aligned value).
     * imm12 = 0xFFC >> 2 = 0x3FF
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0xB9400000);
    apply_reloc(insn, 0, R_AARCH64_LDST32_ABS_LO12_NC, 0x400FFC, 0);
    result = read32le(insn);
    ASSERT_EQ((result >> 10) & 0xfff, 0x3FF);
}

/* ===== R_AARCH64_LDST128_ABS_LO12_NC tests ===== */

TEST(ldst128_lo12_aligned)
{
    /*
     * LDR Q0, [X0] with 128-bit aligned address.
     *   sym_addr = 0x401010, lo12 = 0x010
     *   imm12 = 0x010 >> 4 = 0x001
     *   LDR Q template: 0x3DC00000
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x3DC00000);
    apply_reloc(insn, 0, R_AARCH64_LDST128_ABS_LO12_NC, 0x401010, 0);
    result = read32le(insn);
    ASSERT_EQ((result >> 10) & 0xfff, 0x001);
}

TEST(ldst128_lo12_max)
{
    /*
     * lo12 = 0xFF0 (max 16-byte aligned value).
     * imm12 = 0xFF0 >> 4 = 0x0FF
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x3DC00000);
    apply_reloc(insn, 0, R_AARCH64_LDST128_ABS_LO12_NC, 0x400FF0, 0);
    result = read32le(insn);
    ASSERT_EQ((result >> 10) & 0xfff, 0x0FF);
}

TEST(ldst128_lo12_with_addend)
{
    /*
     * sym_addr = 0x401000, addend = 0x020
     * S+A = 0x401020, lo12 = 0x020
     * imm12 = 0x020 >> 4 = 0x002
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x3DC00000);
    apply_reloc(insn, 0, R_AARCH64_LDST128_ABS_LO12_NC, 0x401000, 0x020);
    result = read32le(insn);
    ASSERT_EQ((result >> 10) & 0xfff, 0x002);
}

/* ===== R_AARCH64_CONDBR19 tests ===== */

TEST(condbr19_forward)
{
    /*
     * B.EQ forward by 0x100 bytes.
     *   B.cond template: 0x54000000 (B.EQ)
     *   patch_addr = 0x400000, sym_addr = 0x400100
     *   offset = 0x100, words = 0x100 / 4 = 0x40
     *   bits [23:5] = 0x40
     *   Expected: 0x54000000 | (0x40 << 5) = 0x54000800
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x54000000);
    apply_reloc(insn, 0x400000, R_AARCH64_CONDBR19, 0x400100, 0);
    result = read32le(insn);
    ASSERT_EQ(result, 0x54000800);
}

TEST(condbr19_backward)
{
    /*
     * B.EQ backward by 0x100 bytes.
     *   offset = -0x100, words = -0x40
     *   imm19 = -0x40 & 0x7FFFF = 0x7FFC0
     *   bits [23:5] = 0x7FFC0
     *   Expected: 0x54000000 | (0x7FFC0 << 5) = 0x54FFF800
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x54000000);
    apply_reloc(insn, 0x400100, R_AARCH64_CONDBR19, 0x400000, 0);
    result = read32le(insn);
    ASSERT_EQ(result, 0x54FFF800);
}

TEST(condbr19_preserves_cond)
{
    /*
     * B.NE (cond = 1): template = 0x54000001.
     * After relocation the low 5 bits must remain 0x01.
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x54000001); /* B.NE */
    apply_reloc(insn, 0x400000, R_AARCH64_CONDBR19, 0x400010, 0);
    result = read32le(insn);
    ASSERT_EQ(result & 0x1f, 0x01);
}

TEST(condbr19_cbz)
{
    /*
     * CBZ W0, target.
     *   CBZ template: 0x34000000
     *   forward 0x80 bytes = 0x20 words
     *   Expected: 0x34000000 | (0x20 << 5) = 0x34000400
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x34000000);
    apply_reloc(insn, 0x400000, R_AARCH64_CONDBR19, 0x400080, 0);
    result = read32le(insn);
    ASSERT_EQ(result, 0x34000400);
}

/* ===== R_AARCH64_TSTBR14 tests ===== */

TEST(tstbr14_forward)
{
    /*
     * TBZ W0, #0, target.
     *   TBZ template: 0x36000000
     *   forward 0x40 bytes = 0x10 words
     *   bits [18:5] = 0x10
     *   Expected: 0x36000000 | (0x10 << 5) = 0x36000200
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x36000000);
    apply_reloc(insn, 0x400000, R_AARCH64_TSTBR14, 0x400040, 0);
    result = read32le(insn);
    ASSERT_EQ(result, 0x36000200);
}

TEST(tstbr14_backward)
{
    /*
     * TBZ backward by 0x40 bytes.
     *   offset = -0x40, words = -0x10
     *   imm14 = -0x10 & 0x3FFF = 0x3FF0
     *   Expected: 0x36000000 | (0x3FF0 << 5) = 0x3607FE00
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x36000000);
    apply_reloc(insn, 0x400040, R_AARCH64_TSTBR14, 0x400000, 0);
    result = read32le(insn);
    ASSERT_EQ(result, 0x3607FE00);
}

TEST(tstbr14_preserves_rt_and_bit)
{
    /*
     * TBNZ W5, #3, target.
     *   TBNZ template: 0x37180005 (bit_pos=3 in b5:b40, rt=5)
     *   Forward 0x08 bytes = 2 words.
     *   imm14 field should be 2, rest preserved.
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x37180005);
    apply_reloc(insn, 0x400000, R_AARCH64_TSTBR14, 0x400008, 0);
    result = read32le(insn);
    ASSERT_EQ(result & 0x1f, 5);          /* rt = 5 */
    ASSERT_EQ((result >> 5) & 0x3fff, 2); /* imm14 = 2 */
}

/* ===== R_AARCH64_ADR_PREL_LO21 tests ===== */

TEST(adr_lo21_forward)
{
    /*
     * ADR X0, target.
     *   ADR template: 0x10000000
     *   patch_addr = 0x400000, sym_addr = 0x400005
     *   val = 5
     *   immlo = 5 & 3 = 1, immhi = (5 >> 2) & 0x7ffff = 1
     *   Expected: 0x10000000 | (1 << 29) | (1 << 5) = 0x30000020
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x10000000);
    apply_reloc(insn, 0x400000, R_AARCH64_ADR_PREL_LO21, 0x400005, 0);
    result = read32le(insn);
    ASSERT_EQ(result, 0x30000020);
}

TEST(adr_lo21_backward)
{
    /*
     * ADR X0 backward by 4 bytes.
     *   val = -4
     *   immlo = -4 & 3 = 0, immhi = (-4 >> 2) & 0x7ffff = 0x7FFFF
     *   Expected: 0x10000000 | (0 << 29) | (0x7FFFF << 5) = 0x10FFFFE0
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x10000000);
    apply_reloc(insn, 0x400004, R_AARCH64_ADR_PREL_LO21, 0x400000, 0);
    result = read32le(insn);
    ASSERT_EQ(result, 0x10FFFFE0);
}

TEST(adr_lo21_preserves_rd)
{
    /*
     * ADR X5 with offset 0.
     * rd = 5 must be preserved.
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x10000005);
    apply_reloc(insn, 0x400000, R_AARCH64_ADR_PREL_LO21, 0x400000, 0);
    result = read32le(insn);
    ASSERT_EQ(result & 0x1f, 5);
}

/* ===== R_AARCH64_PREL32 tests ===== */

TEST(prel32_forward)
{
    /*
     * 32-bit PC-relative.
     * patch_addr = 0x400000, sym_addr = 0x400100
     * result = 0x100
     */
    u8 data[4];
    u32 result;

    memset(data, 0, sizeof(data));
    apply_reloc(data, 0x400000, R_AARCH64_PREL32, 0x400100, 0);
    result = read32le(data);
    ASSERT_EQ(result, 0x100);
}

TEST(prel32_backward)
{
    /*
     * patch_addr = 0x400100, sym_addr = 0x400000
     * result = -0x100 = 0xFFFFFF00 as u32
     */
    u8 data[4];
    u32 result;

    memset(data, 0, sizeof(data));
    apply_reloc(data, 0x400100, R_AARCH64_PREL32, 0x400000, 0);
    result = read32le(data);
    ASSERT_EQ(result, 0xFFFFFF00);
}

TEST(prel32_with_addend)
{
    u8 data[4];
    u32 result;

    memset(data, 0, sizeof(data));
    apply_reloc(data, 0x400000, R_AARCH64_PREL32, 0x400100, 8);
    result = read32le(data);
    ASSERT_EQ(result, 0x108);
}

/* ===== R_AARCH64_PREL16 tests ===== */

TEST(prel16_forward)
{
    /*
     * 16-bit PC-relative.
     * patch_addr = 0x400000, sym_addr = 0x400080
     * result = 0x80
     */
    u8 data[2];
    u16 result;

    memset(data, 0, sizeof(data));
    apply_reloc(data, 0x400000, R_AARCH64_PREL16, 0x400080, 0);
    result = read16le(data);
    ASSERT_EQ(result, 0x80);
}

TEST(prel16_backward)
{
    /*
     * patch_addr = 0x400080, sym_addr = 0x400000
     * result = -0x80 = 0xFF80 as u16
     */
    u8 data[2];
    u16 result;

    memset(data, 0, sizeof(data));
    apply_reloc(data, 0x400080, R_AARCH64_PREL16, 0x400000, 0);
    result = read16le(data);
    ASSERT_EQ(result, 0xFF80);
}

/* ===== R_AARCH64_MOVW_UABS_G0 tests ===== */

TEST(movw_g0_simple)
{
    /*
     * MOVZ X0, #imm16, lsl #0
     *   MOVZ template: 0xD2800000
     *   sym_addr = 0x12345678
     *   bits [15:0] = 0x5678
     *   imm16 field at bits [20:5] = 0x5678 << 5
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0xD2800000);
    apply_reloc(insn, 0, R_AARCH64_MOVW_UABS_G0, 0x12345678, 0);
    result = read32le(insn);
    ASSERT_EQ((result >> 5) & 0xffff, 0x5678);
}

TEST(movw_g0_preserves_opcode)
{
    u8 insn[4];
    u32 result;

    write32le(insn, 0xD2800000);
    apply_reloc(insn, 0, R_AARCH64_MOVW_UABS_G0, 0xFFFF, 0);
    result = read32le(insn);
    /* top bits and rd must be preserved */
    ASSERT_EQ(result & 0x1f, 0);
    ASSERT_EQ(result & 0xffe00000, 0xD2800000 & 0xffe00000);
}

/* ===== R_AARCH64_MOVW_UABS_G1 tests ===== */

TEST(movw_g1_simple)
{
    /*
     * MOVK X0, #imm16, lsl #16
     *   MOVK template: 0xF2A00000
     *   sym_addr = 0x12345678
     *   bits [31:16] = 0x1234
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0xF2A00000);
    apply_reloc(insn, 0, R_AARCH64_MOVW_UABS_G1, 0x12345678, 0);
    result = read32le(insn);
    ASSERT_EQ((result >> 5) & 0xffff, 0x1234);
}

/* ===== R_AARCH64_MOVW_UABS_G2 tests ===== */

TEST(movw_g2_simple)
{
    /*
     * MOVK X0, #imm16, lsl #32
     *   MOVK template: 0xF2C00000
     *   sym_addr = 0xABCD12345678
     *   bits [47:32] = 0xABCD
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0xF2C00000);
    apply_reloc(insn, 0, R_AARCH64_MOVW_UABS_G2, 0xABCD12345678UL, 0);
    result = read32le(insn);
    ASSERT_EQ((result >> 5) & 0xffff, 0xABCD);
}

/* ===== R_AARCH64_MOVW_UABS_G3 tests ===== */

TEST(movw_g3_simple)
{
    /*
     * MOVK X0, #imm16, lsl #48
     *   MOVK template: 0xF2E00000
     *   sym_addr = 0xFFFF000012345678
     *   bits [63:48] = 0xFFFF
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0xF2E00000);
    apply_reloc(insn, 0, R_AARCH64_MOVW_UABS_G3, 0xFFFF000012345678UL, 0);
    result = read32le(insn);
    ASSERT_EQ((result >> 5) & 0xffff, 0xFFFF);
}

TEST(movw_g3_with_addend)
{
    /*
     * sym_addr = 0xFFFE000000000000, addend = 0x1000000000000
     * S+A = 0xFFFF000000000000
     * bits [63:48] = 0xFFFF
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0xF2E00000);
    apply_reloc(insn, 0, R_AARCH64_MOVW_UABS_G3,
                0xFFFE000000000000UL, 0x1000000000000L);
    result = read32le(insn);
    ASSERT_EQ((result >> 5) & 0xffff, 0xFFFF);
}

/* ===== MOVZ/MOVK full 64-bit address reconstruction ===== */

TEST(movw_full_addr)
{
    /*
     * Reconstruct 0xFFFF0000ABCD1234 from G0..G3.
     */
    u8 g0[4];
    u8 g1[4];
    u8 g2[4];
    u8 g3[4];
    u64 addr;
    u64 reconstructed;
    u16 v0;
    u16 v1;
    u16 v2;
    u16 v3;
    u32 r;

    addr = 0xFFFF0000ABCD1234UL;
    write32le(g0, 0xD2800000);
    write32le(g1, 0xF2A00000);
    write32le(g2, 0xF2C00000);
    write32le(g3, 0xF2E00000);

    apply_reloc(g0, 0, R_AARCH64_MOVW_UABS_G0, addr, 0);
    apply_reloc(g1, 0, R_AARCH64_MOVW_UABS_G1, addr, 0);
    apply_reloc(g2, 0, R_AARCH64_MOVW_UABS_G2, addr, 0);
    apply_reloc(g3, 0, R_AARCH64_MOVW_UABS_G3, addr, 0);

    r = read32le(g0);
    v0 = (u16)((r >> 5) & 0xffff);
    r = read32le(g1);
    v1 = (u16)((r >> 5) & 0xffff);
    r = read32le(g2);
    v2 = (u16)((r >> 5) & 0xffff);
    r = read32le(g3);
    v3 = (u16)((r >> 5) & 0xffff);

    reconstructed = (u64)v0 | ((u64)v1 << 16) |
                    ((u64)v2 << 32) | ((u64)v3 << 48);
    ASSERT_EQ(reconstructed, addr);
}

/* ===== R_AARCH64_LDST8_ABS_LO12_NC tests ===== */

TEST(ldst8_lo12_simple)
{
    /*
     * LDRB with byte address.
     *   sym_addr = 0x401042, lo12 = 0x042
     *   imm12 = 0x042 (no shift for byte)
     *   LDRB template: 0x39400000
     */
    u8 insn[4];
    u32 result;

    write32le(insn, 0x39400000);
    apply_reloc(insn, 0, R_AARCH64_LDST8_ABS_LO12_NC, 0x401042, 0);
    result = read32le(insn);
    ASSERT_EQ((result >> 10) & 0xfff, 0x042);
}

/* ===== main ===== */

int main(void)
{
    printf("test_reloc:\n");

    /* R_AARCH64_CALL26 (BL) */
    RUN_TEST(call26_forward_small);
    RUN_TEST(call26_forward_large);
    RUN_TEST(call26_backward);
    RUN_TEST(call26_with_addend);
    RUN_TEST(call26_preserves_opcode);
    RUN_TEST(call26_self_reference);

    /* R_AARCH64_JUMP26 (B) */
    RUN_TEST(jump26_forward);
    RUN_TEST(jump26_backward);
    RUN_TEST(jump26_preserves_opcode);
    RUN_TEST(jump26_with_addend);

    /* R_AARCH64_ABS64 */
    RUN_TEST(abs64_simple);
    RUN_TEST(abs64_with_addend);
    RUN_TEST(abs64_negative_addend);
    RUN_TEST(abs64_high_address);
    RUN_TEST(abs64_overwrites_existing);

    /* R_AARCH64_ADR_PREL_PG_HI21 (ADRP) */
    RUN_TEST(adrp_same_page);
    RUN_TEST(adrp_next_page);
    RUN_TEST(adrp_four_pages_forward);
    RUN_TEST(adrp_preserves_rd);
    RUN_TEST(adrp_with_addend);

    /* R_AARCH64_ADD_ABS_LO12_NC */
    RUN_TEST(add_lo12_zero);
    RUN_TEST(add_lo12_nonzero);
    RUN_TEST(add_lo12_max);
    RUN_TEST(add_lo12_preserves_rd_rn);
    RUN_TEST(add_lo12_with_addend);
    RUN_TEST(add_lo12_clears_old_imm);

    /* ADRP + ADD pair */
    RUN_TEST(adrp_add_pair);

    /* R_AARCH64_NONE */
    RUN_TEST(none_reloc_noop);

    /* R_AARCH64_ABS32 */
    RUN_TEST(abs32_simple);
    RUN_TEST(abs32_with_addend);

    /* R_AARCH64_LDST16_ABS_LO12_NC */
    RUN_TEST(ldst16_lo12_aligned);
    RUN_TEST(ldst16_lo12_max);
    RUN_TEST(ldst16_lo12_with_addend);

    /* R_AARCH64_LDST32_ABS_LO12_NC */
    RUN_TEST(ldst32_lo12_aligned);
    RUN_TEST(ldst32_lo12_max);

    /* R_AARCH64_LDST128_ABS_LO12_NC */
    RUN_TEST(ldst128_lo12_aligned);
    RUN_TEST(ldst128_lo12_max);
    RUN_TEST(ldst128_lo12_with_addend);

    /* R_AARCH64_CONDBR19 */
    RUN_TEST(condbr19_forward);
    RUN_TEST(condbr19_backward);
    RUN_TEST(condbr19_preserves_cond);
    RUN_TEST(condbr19_cbz);

    /* R_AARCH64_TSTBR14 */
    RUN_TEST(tstbr14_forward);
    RUN_TEST(tstbr14_backward);
    RUN_TEST(tstbr14_preserves_rt_and_bit);

    /* R_AARCH64_ADR_PREL_LO21 */
    RUN_TEST(adr_lo21_forward);
    RUN_TEST(adr_lo21_backward);
    RUN_TEST(adr_lo21_preserves_rd);

    /* R_AARCH64_PREL32 */
    RUN_TEST(prel32_forward);
    RUN_TEST(prel32_backward);
    RUN_TEST(prel32_with_addend);

    /* R_AARCH64_PREL16 */
    RUN_TEST(prel16_forward);
    RUN_TEST(prel16_backward);

    /* R_AARCH64_MOVW_UABS_G0 */
    RUN_TEST(movw_g0_simple);
    RUN_TEST(movw_g0_preserves_opcode);

    /* R_AARCH64_MOVW_UABS_G1 */
    RUN_TEST(movw_g1_simple);

    /* R_AARCH64_MOVW_UABS_G2 */
    RUN_TEST(movw_g2_simple);

    /* R_AARCH64_MOVW_UABS_G3 */
    RUN_TEST(movw_g3_simple);
    RUN_TEST(movw_g3_with_addend);

    /* MOVZ/MOVK full address reconstruction */
    RUN_TEST(movw_full_addr);

    /* R_AARCH64_LDST8_ABS_LO12_NC */
    RUN_TEST(ldst8_lo12_simple);

    TEST_SUMMARY();
    return tests_failed;
}
