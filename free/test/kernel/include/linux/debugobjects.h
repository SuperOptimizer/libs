/* SPDX-License-Identifier: GPL-2.0 */
/* Stub debugobjects.h for free-cc kernel compilation testing */
#ifndef _LINUX_DEBUGOBJECTS_H
#define _LINUX_DEBUGOBJECTS_H

enum debug_obj_state {
    ODEBUG_STATE_NONE,
    ODEBUG_STATE_INIT,
    ODEBUG_STATE_INACTIVE,
    ODEBUG_STATE_ACTIVE,
    ODEBUG_STATE_DESTROYED,
    ODEBUG_STATE_NOTAVAILABLE,
    ODEBUG_STATE_MAX
};

struct debug_obj_descr {
    const char *name;
};

#define debug_object_init(obj, descr) do {} while (0)
#define debug_object_activate(obj, descr) do {} while (0)
#define debug_object_deactivate(obj, descr) do {} while (0)
#define debug_object_destroy(obj, descr) do {} while (0)
#define debug_object_free(obj, descr) do {} while (0)

#endif /* _LINUX_DEBUGOBJECTS_H */
