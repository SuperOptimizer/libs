/*
 * setjmp.S - setjmp/longjmp for aarch64
 *
 * jmp_buf layout (21 x 8 bytes):
 *   [0]  x19    [10] x29 (FP)
 *   [1]  x20    [11] x30 (LR)
 *   [2]  x21    [12] sp
 *   [3]  x22    [13] d8
 *   [4]  x23    [14] d9
 *   [5]  x24    [15] d10
 *   [6]  x25    [16] d11
 *   [7]  x26    [17] d12
 *   [8]  x27    [18] d13
 *   [9]  x28    [19] d14
 *                [20] d15
 */

.section .text

/* ------------------------------------------------------------------ */
/* int setjmp(jmp_buf env)                                             */
/* x0 = pointer to jmp_buf                                             */
/* Returns 0 on direct call, non-zero on longjmp return.               */
/* ------------------------------------------------------------------ */

.global setjmp
.type setjmp, %function
setjmp:
    /* save callee-saved GPRs */
    stp     x19, x20, [x0, #0]
    stp     x21, x22, [x0, #16]
    stp     x23, x24, [x0, #32]
    stp     x25, x26, [x0, #48]
    stp     x27, x28, [x0, #64]
    stp     x29, x30, [x0, #80]
    mov     x1, sp
    str     x1, [x0, #96]

    /* save callee-saved FP regs */
    stp     d8,  d9,  [x0, #104]
    stp     d10, d11, [x0, #120]
    stp     d12, d13, [x0, #136]
    stp     d14, d15, [x0, #152]

    /* return 0 */
    mov     x0, #0
    ret
.size setjmp, . - setjmp

/* ------------------------------------------------------------------ */
/* void longjmp(jmp_buf env, int val)                                  */
/* x0 = pointer to jmp_buf, x1 = return value (0 becomes 1)           */
/* ------------------------------------------------------------------ */

.global longjmp
.type longjmp, %function
longjmp:
    /* restore callee-saved GPRs */
    ldp     x19, x20, [x0, #0]
    ldp     x21, x22, [x0, #16]
    ldp     x23, x24, [x0, #32]
    ldp     x25, x26, [x0, #48]
    ldp     x27, x28, [x0, #64]
    ldp     x29, x30, [x0, #80]
    ldr     x2, [x0, #96]
    mov     sp, x2

    /* restore callee-saved FP regs */
    ldp     d8,  d9,  [x0, #104]
    ldp     d10, d11, [x0, #120]
    ldp     d12, d13, [x0, #136]
    ldp     d14, d15, [x0, #152]

    /* return val; if val == 0, return 1 instead */
    mov     x0, x1
    cmp     x0, #0
    cinc    x0, x0, eq
    ret
.size longjmp, . - longjmp
