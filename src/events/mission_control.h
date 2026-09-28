#ifndef MISSION_CONTROL_H
#define MISSION_CONTROL_H

#include "../core_types.h"

void connection_handler(uint32_t type, void *data, size_t data_length, void *context, int cid);

// Area: mission_control.c observes Dock and SkyLight Mission Control changes.
// Threads: main-thread callbacks post events; the event loop updates mode.
// State: g_mission_control_mode.
// Callers: startup, event handlers and navigation.
// Calls: AX, SkyLight and event_loop_post.

enum mission_control_mode
{
    MISSION_CONTROL_MODE_INACTIVE           = 0,
    MISSION_CONTROL_MODE_SHOW               = 1,
    MISSION_CONTROL_MODE_SHOW_ALL_WINDOWS   = 2,
    MISSION_CONTROL_MODE_SHOW_FRONT_WINDOWS = 3,
    MISSION_CONTROL_MODE_SHOW_DESKTOP       = 4
};

void mission_control_observe(void);
void mission_control_unobserve(void);
bool mission_control_is_active(void);

extern enum mission_control_mode g_mission_control_mode;

#endif
