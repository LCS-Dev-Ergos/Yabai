#ifndef NAVIGATION_ACTIVATION_H
#define NAVIGATION_ACTIVATION_H

// Navigation activation (activation.c): gives the destination window focus
// without holding the event loop for the 40 ms that
// window_manager_focus_window_without_raise sleeps, and raises windows of
// applications that show another window on a different display.
//
// Thread: event loop. The second half of a focus change between two windows
// of one application runs 10 ms later as a SPACE_NAVIGATION_FOCUS event.
// State: g_space_navigation_focus, the generation that cancels a deferred
// focus and the window it will focus.
// Callers: step focuses or raises; step and command cancel. Resuming is a
// hook, see hooks.h.
// Calls: space_navigation_focus_schedule in command.c for the delay, and the
// focus functions of window_manager.h.

static void space_navigation_focus_cancel(void);
static void space_navigation_focus_window(ProcessSerialNumber *psn, uint32_t window_id);
static void space_navigation_raise_window(ProcessSerialNumber *psn, uint32_t window_id, AXUIElementRef ref);

#endif
