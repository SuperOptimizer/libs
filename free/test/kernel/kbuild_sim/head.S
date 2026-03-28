/*
 * head.S - Kernel entry point (simplified)
 * Exercises: macros, system register access, basic asm
 */

#define __KERNEL__

.macro SAVE_REGS
    stp x29, x30, [sp, #-16]!
    mov x29, sp
.endm

.macro RESTORE_REGS
    ldp x29, x30, [sp], #16
.endm

    .section .text
    .globl _start
    .globl kernel_main
    .type _start, %function
_start:
    /* Set up stack pointer (must be 16-byte aligned) */
    adrp x0, _stack_top
    add  x0, x0, :lo12:_stack_top
    /* Align down to 16 bytes: clear bottom 4 bits using
     * two shifts since assembler lacks bic with immediate */
    lsr  x0, x0, #4
    lsl  x0, x0, #4
    mov  sp, x0

    /* Read system counter frequency (CNTFRQ_EL0) */
    mrs  x1, cntfrq_el0

    /* Data memory barrier */
    dmb  sy
    isb

    /* Call kernel_main - returns fail count in x0 */
    bl   kernel_main

    /* Exit via syscall - x0 already has the exit code */
    mov  x8, #93
    svc  #0

    .size _start, . - _start

    .globl arch_halt
    .type arch_halt, %function
arch_halt:
    SAVE_REGS
    wfi
    RESTORE_REGS
    ret
    .size arch_halt, . - arch_halt

    /* Read cycle counter */
    .globl read_cycles
    .type read_cycles, %function
read_cycles:
    mrs  x0, cntvct_el0
    ret
    .size read_cycles, . - read_cycles

    .section .bss
    .space 8
_stack_bottom:
    .space 4096
_stack_top:
