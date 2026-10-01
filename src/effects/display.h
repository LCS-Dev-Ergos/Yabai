#ifndef EFFECTS_DISPLAY_H
#define EFFECTS_DISPLAY_H

#include "../core_types.h"

// Display facts the effects depend on (display.m): the refresh interval they
// are timed with, Reduce Motion and Reduce Transparency. Each call asks the
// system.
//
// Thread: any; the event loop is the only caller.
// Callers: step, window fade, snapshot (Reduce Transparency).

static float space_navigation_frame_interval(uint32_t display);
static bool space_navigation_reduce_motion(void);
static bool space_navigation_reduce_transparency(void);

#endif
