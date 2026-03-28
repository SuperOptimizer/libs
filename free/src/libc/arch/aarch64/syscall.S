/*
 * syscall.S - Generic syscall wrapper for aarch64-linux
 *
 * long __syscall(long num, long a1, long a2, long a3,
 *                long a4, long a5, long a6);
 *
 * C calling convention passes arguments in x0-x7.
 * Linux syscall convention: number in x8, arguments in x0-x5.
 */

.section .text
.global __syscall
.type __syscall, %function
__syscall:
    mov     x8, x0              /* syscall number */
    mov     x0, x1              /* arg1 */
    mov     x1, x2              /* arg2 */
    mov     x2, x3              /* arg3 */
    mov     x3, x4              /* arg4 */
    mov     x4, x5              /* arg5 */
    mov     x5, x6              /* arg6 */
    svc     #0
    ret
.size __syscall, . - __syscall
