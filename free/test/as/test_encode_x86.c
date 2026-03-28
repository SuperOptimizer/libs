/*
 * test_encode_x86.c - Tests for x86_64 instruction encoder.
 * Validates that instruction encodings match expected byte sequences.
 */

#include "test.h"
#include "x86_64.h"

/* ---- helper to check encoded bytes ---- */
static void check_bytes(const char *name, u8 *buf, int len,
                        const u8 *expected, int exp_len)
{
    int i;

    if (len != exp_len) {
        printf("  FAIL %s: length %d != expected %d\n",
               name, len, exp_len);
        current_failed = 1;
        return;
    }
    for (i = 0; i < len; i++) {
        if (buf[i] != expected[i]) {
            printf("  FAIL %s: byte[%d] = 0x%02x, expected 0x%02x\n",
                   name, i, buf[i], expected[i]);
            current_failed = 1;
            return;
        }
    }
}

/* ---- MOV tests ---- */

TEST(mov_rr_rax_rcx)
{
    /* MOV rax, rcx: REX.W(48) 89 C8 (mod=11 src=rcx dst=rax) */
    u8 buf[X86_MAX_INSN];
    int len;
    u8 expected[] = {0x48, 0x89, 0xC8};

    len = x86_mov_rr(buf, X86_RAX, X86_RCX);
    check_bytes("mov rax, rcx", buf, len, expected, 3);
}

TEST(mov_rr_r8_r15)
{
    /* MOV r8, r15: REX.WRB(4D) 89 F8 */
    u8 buf[X86_MAX_INSN];
    int len;
    u8 expected[] = {0x4D, 0x89, 0xF8};

    len = x86_mov_rr(buf, X86_R8, X86_R15);
    check_bytes("mov r8, r15", buf, len, expected, 3);
}

TEST(mov_ri_zero)
{
    /* MOV rax, 0 => XOR eax, eax: 31 C0 */
    u8 buf[X86_MAX_INSN];
    int len;
    u8 expected[] = {0x31, 0xC0};

    len = x86_mov_ri(buf, X86_RAX, 0);
    check_bytes("mov rax, 0 (xor)", buf, len, expected, 2);
}

TEST(mov_ri32_small)
{
    /* MOV eax, 42: B8 2A 00 00 00 */
    u8 buf[X86_MAX_INSN];
    int len;
    u8 expected[] = {0xB8, 0x2A, 0x00, 0x00, 0x00};

    len = x86_mov_ri(buf, X86_RAX, 42);
    check_bytes("mov eax, 42", buf, len, expected, 5);
}

TEST(mov_ri_neg)
{
    /* MOV rax, -1: REX.W C7 C0 FF FF FF FF */
    u8 buf[X86_MAX_INSN];
    int len;
    u8 expected[] = {0x48, 0xC7, 0xC0, 0xFF, 0xFF, 0xFF, 0xFF};

    len = x86_mov_ri(buf, X86_RAX, -1);
    check_bytes("mov rax, -1", buf, len, expected, 7);
}

/* ---- Push/Pop tests ---- */

TEST(push_rax)
{
    u8 buf[X86_MAX_INSN];
    int len;
    u8 expected[] = {0x50};

    len = x86_push_r(buf, X86_RAX);
    check_bytes("push rax", buf, len, expected, 1);
}

TEST(pop_rbp)
{
    u8 buf[X86_MAX_INSN];
    int len;
    u8 expected[] = {0x5D};

    len = x86_pop_r(buf, X86_RBP);
    check_bytes("pop rbp", buf, len, expected, 1);
}

TEST(push_r12)
{
    /* PUSH r12: REX.B(41) 54 */
    u8 buf[X86_MAX_INSN];
    int len;
    u8 expected[] = {0x41, 0x54};

    len = x86_push_r(buf, X86_R12);
    check_bytes("push r12", buf, len, expected, 2);
}

/* ---- Arithmetic tests ---- */

TEST(add_rr)
{
    /* ADD rax, rcx: REX.W 01 C8 */
    u8 buf[X86_MAX_INSN];
    int len;
    u8 expected[] = {0x48, 0x01, 0xC8};

    len = x86_add_rr(buf, X86_RAX, X86_RCX);
    check_bytes("add rax, rcx", buf, len, expected, 3);
}

TEST(sub_ri_imm8)
{
    /* SUB rsp, 8: REX.W 83 EC 08 */
    u8 buf[X86_MAX_INSN];
    int len;
    u8 expected[] = {0x48, 0x83, 0xEC, 0x08};

    len = x86_sub_ri(buf, X86_RSP, 8);
    check_bytes("sub rsp, 8", buf, len, expected, 4);
}

TEST(imul_rr)
{
    /* IMUL rax, rcx: REX.W 0F AF C1 */
    u8 buf[X86_MAX_INSN];
    int len;
    u8 expected[] = {0x48, 0x0F, 0xAF, 0xC1};

    len = x86_imul_rr(buf, X86_RAX, X86_RCX);
    check_bytes("imul rax, rcx", buf, len, expected, 4);
}

TEST(cqo_insn)
{
    /* CQO: 48 99 */
    u8 buf[X86_MAX_INSN];
    int len;
    u8 expected[] = {0x48, 0x99};

    len = x86_cqo(buf);
    check_bytes("cqo", buf, len, expected, 2);
}

/* ---- Comparison tests ---- */

TEST(cmp_rr)
{
    /* CMP rax, rcx: REX.W 39 C8 */
    u8 buf[X86_MAX_INSN];
    int len;
    u8 expected[] = {0x48, 0x39, 0xC8};

    len = x86_cmp_rr(buf, X86_RAX, X86_RCX);
    check_bytes("cmp rax, rcx", buf, len, expected, 3);
}

TEST(cmp_ri_imm8)
{
    /* CMP rax, 0: REX.W 83 F8 00 */
    u8 buf[X86_MAX_INSN];
    int len;
    u8 expected[] = {0x48, 0x83, 0xF8, 0x00};

    len = x86_cmp_ri(buf, X86_RAX, 0);
    check_bytes("cmp rax, 0", buf, len, expected, 4);
}

/* ---- Branch tests ---- */

TEST(ret_insn)
{
    u8 buf[X86_MAX_INSN];
    int len;
    u8 expected[] = {0xC3};

    len = x86_ret(buf);
    check_bytes("ret", buf, len, expected, 1);
}

TEST(jmp_rel32)
{
    /* JMP +0: E9 00 00 00 00 */
    u8 buf[X86_MAX_INSN];
    int len;
    u8 expected[] = {0xE9, 0x00, 0x00, 0x00, 0x00};

    len = x86_jmp_rel32(buf, 0);
    check_bytes("jmp +0", buf, len, expected, 5);
}

TEST(call_rel32)
{
    /* CALL +0: E8 00 00 00 00 */
    u8 buf[X86_MAX_INSN];
    int len;
    u8 expected[] = {0xE8, 0x00, 0x00, 0x00, 0x00};

    len = x86_call_rel32(buf, 0);
    check_bytes("call +0", buf, len, expected, 5);
}

/* ---- Shift tests ---- */

TEST(shl_cl)
{
    /* SHL rax, cl: REX.W D3 E0 */
    u8 buf[X86_MAX_INSN];
    int len;
    u8 expected[] = {0x48, 0xD3, 0xE0};

    len = x86_shl_cl(buf, X86_RAX);
    check_bytes("shl rax, cl", buf, len, expected, 3);
}

/* ---- Logical tests ---- */

TEST(xor_rr32)
{
    /* XOR eax, eax: 31 C0 */
    u8 buf[X86_MAX_INSN];
    int len;
    u8 expected[] = {0x31, 0xC0};

    len = x86_xor_rr32(buf, X86_RAX, X86_RAX);
    check_bytes("xor eax, eax", buf, len, expected, 2);
}

TEST(not_r64)
{
    /* NOT rax: REX.W F7 D0 */
    u8 buf[X86_MAX_INSN];
    int len;
    u8 expected[] = {0x48, 0xF7, 0xD0};

    len = x86_not_r(buf, X86_RAX);
    check_bytes("not rax", buf, len, expected, 3);
}

/* ---- Memory tests ---- */

TEST(mov_rm_rbp_disp)
{
    /* MOV rax, [rbp-8]: REX.W 8B 45 F8 */
    u8 buf[X86_MAX_INSN];
    int len;
    u8 expected[] = {0x48, 0x8B, 0x45, 0xF8};

    len = x86_mov_rm(buf, X86_RAX, X86_RBP, -8);
    check_bytes("mov rax, [rbp-8]", buf, len, expected, 4);
}

TEST(mov_mr_rbp_disp)
{
    /* MOV [rbp-8], rax: REX.W 89 45 F8 */
    u8 buf[X86_MAX_INSN];
    int len;
    u8 expected[] = {0x48, 0x89, 0x45, 0xF8};

    len = x86_mov_mr(buf, X86_RBP, -8, X86_RAX);
    check_bytes("mov [rbp-8], rax", buf, len, expected, 4);
}

TEST(lea_rbp_disp)
{
    /* LEA rax, [rbp-16]: REX.W 8D 45 F0 */
    u8 buf[X86_MAX_INSN];
    int len;
    u8 expected[] = {0x48, 0x8D, 0x45, 0xF0};

    len = x86_lea(buf, X86_RAX, X86_RBP, -16);
    check_bytes("lea rax, [rbp-16]", buf, len, expected, 4);
}

/* ---- Misc ---- */

TEST(nop_insn)
{
    u8 buf[X86_MAX_INSN];
    int len;
    u8 expected[] = {0x90};

    len = x86_nop(buf);
    check_bytes("nop", buf, len, expected, 1);
}

TEST(syscall_insn)
{
    u8 buf[X86_MAX_INSN];
    int len;
    u8 expected[] = {0x0F, 0x05};

    len = x86_syscall(buf);
    check_bytes("syscall", buf, len, expected, 2);
}

int main(void)
{
    printf("test_encode_x86:\n");

    RUN_TEST(mov_rr_rax_rcx);
    RUN_TEST(mov_rr_r8_r15);
    RUN_TEST(mov_ri_zero);
    RUN_TEST(mov_ri32_small);
    RUN_TEST(mov_ri_neg);
    RUN_TEST(push_rax);
    RUN_TEST(pop_rbp);
    RUN_TEST(push_r12);
    RUN_TEST(add_rr);
    RUN_TEST(sub_ri_imm8);
    RUN_TEST(imul_rr);
    RUN_TEST(cqo_insn);
    RUN_TEST(cmp_rr);
    RUN_TEST(cmp_ri_imm8);
    RUN_TEST(ret_insn);
    RUN_TEST(jmp_rel32);
    RUN_TEST(call_rel32);
    RUN_TEST(shl_cl);
    RUN_TEST(xor_rr32);
    RUN_TEST(not_r64);
    RUN_TEST(mov_rm_rbp_disp);
    RUN_TEST(mov_mr_rbp_disp);
    RUN_TEST(lea_rbp_disp);
    RUN_TEST(nop_insn);
    RUN_TEST(syscall_insn);

    TEST_SUMMARY();
    return tests_failed ? 1 : 0;
}
