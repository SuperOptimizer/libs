/*
 * setjmp.h - Non-local jumps.
 * Pure C89.
 */

#ifndef _SETJMP_H
#define _SETJMP_H

/*
 * jmp_buf for aarch64 (glibc-compatible layout):
 *
 * glibc defines jmp_buf as struct __jmp_buf_tag[1]:
 *   __jmpbuf:        unsigned long long[22]  = 176 bytes (core regs)
 *   __mask_was_saved: int                    =   4 bytes + 4 pad
 *   __saved_mask:    __sigset_t (1024 bits)  = 128 bytes
 *   Total: 312 bytes = 39 unsigned longs.
 *
 * When linking against system libc setjmp/longjmp, the buffer must
 * be this size or setjmp will corrupt the caller's stack frame.
 */
typedef unsigned long jmp_buf[39];

int  setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

#endif
