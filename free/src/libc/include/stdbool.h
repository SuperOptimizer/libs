#ifndef _STDBOOL_H
#define _STDBOOL_H

#ifndef __cplusplus
/* C23 makes bool/true/false keywords, but for C99/C11 they're macros */
#define bool _Bool
#define true 1
#define false 0
#endif

#define __bool_true_false_are_defined 1

#endif
