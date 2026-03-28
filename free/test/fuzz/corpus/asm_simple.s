/* simple aarch64 assembly for fuzz corpus */
    .arch armv8-a
    .text
    .global _start
    .type _start, %function

_start:
    /* exit(42) */
    mov x0, #42
    mov x8, #94
    svc #0

    .global add
    .type add, %function
add:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    add x0, x0, x1
    ldp x29, x30, [sp], #16
    ret

    .section .rodata
msg:
    .string "hello world"
    .byte 0

    .data
    .global counter
counter:
    .quad 0

    .bss
    .global buffer
buffer:
    .zero 256

    .text
loop:
    sub x0, x0, #1
    cmp x0, #0
    b.ne loop
    ret
