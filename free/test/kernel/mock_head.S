/* Mock kernel head.S - exercises assembler features */
.section ".head.text","ax"

.macro save_and_disable_irqs, flags
    mrs \flags, daif
    msr daifset, #0xf
.endm

.macro restore_irqs, flags
    msr daif, \flags
.endm

.macro adr_l, dst, sym
    adrp \dst, \sym
    add \dst, \dst, :lo12:\sym
.endm

.global _start
.type _start, %function
_start:
    /* Disable interrupts */
    msr daifset, #0xf

    /* Set up stack */
    adrp x0, init_stack
    add x0, x0, :lo12:init_stack
    mov sp, x0

    /* Branch to main */
    bl main_func

    /* Should not return */
    b .

.type main_func, %function
main_func:
    stp x29, x30, [sp, #-16]!
    mov x29, sp

    /* Test system register access */
    mrs x0, currentel

    /* Test barriers */
    dmb ish
    dsb sy
    isb

    /* Test conditional select */
    mov x0, #1
    mov x1, #2
    cmp x0, x1
    csel x2, x0, x1, lt

    /* Test bit manipulation */
    mov x0, #0xFF00
    rbit x1, x0
    clz x2, x0
    rev x3, x0

    /* Test bitfield */
    mov x0, #0xABCD
    ubfm x1, x0, #4, #11

    /* Test atomic */
    adrp x4, test_var
    add x4, x4, :lo12:test_var
1:  ldxr x5, [x4]
    add x5, x5, #1
    stxr w6, x5, [x4]
    cbnz w6, 1b

    /* Test BTI */
    bti c

    /* Test macro expansion */
    save_and_disable_irqs x7
    restore_irqs x7

    /* Test adr_l macro */
    adr_l x8, test_var

    ldp x29, x30, [sp], #16
    ret

.section ".data"
.balign 8
test_var:
    .quad 0

.section ".bss"
.balign 16
init_stack_bottom:
    .space 4096
init_stack:
