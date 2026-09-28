#ifndef MISSION_CONTROL_H
#define MISSION_CONTROL_H

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
static inline bool mission_control_is_active(void);

extern enum mission_control_mode g_mission_control_mode;

#endif
