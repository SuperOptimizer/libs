/*
 * crt.S - C runtime startup for aarch64-linux (freestanding)
 *
 * On entry from the kernel, the stack contains:
 *   [sp]       = argc
 *   [sp + 8]   = argv[0]
 *   ...
 *   [sp + 8 + 8*argc] = NULL  (argv terminator)
 *   [sp + 8 + 8*argc + 8] = envp[0]
 *   ...
 */

.section .text
.global _start
.type _start, %function
_start:
    ldr     x0, [sp]            /* x0 = argc */
    add     x1, sp, #8          /* x1 = argv */
    add     x2, x0, #1          /* argc + 1 (skip argv + NULL terminator) */
    add     x2, x1, x2, lsl #3 /* x2 = envp = argv + (argc + 1) * 8 */

    mov     x9, sp
    bic     x9, x9, #15        /* align stack to 16 bytes */
    mov     sp, x9

    bl      main

    mov     x8, #94             /* __NR_exit_group */
    svc     #0
.size _start, . - _start
