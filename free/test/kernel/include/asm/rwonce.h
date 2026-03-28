/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RWONCE_H
#define _ASM_RWONCE_H

#define READ_ONCE(x)       (x)
#define WRITE_ONCE(x, val) ((x) = (val))

#endif /* _ASM_RWONCE_H */
