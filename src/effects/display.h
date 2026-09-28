#ifndef EFFECTS_DISPLAY_H
#define EFFECTS_DISPLAY_H

// Display facts the effects depend on (display.m): the refresh interval they
// are timed with, and Reduce Motion. Each call asks the system.
//
// Thread: any; the event loop is the only caller.
// Callers: step, window fade.

static float space_navigation_frame_interval(uint32_t display);
static bool space_navigation_reduce_motion(void);

#endif
