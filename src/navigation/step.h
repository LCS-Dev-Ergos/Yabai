#ifndef NAVIGATION_STEP_H
#define NAVIGATION_STEP_H

// Navigation step (step.c): one Desktop switch with its effect and, on the
// last step, the activation of a window.
//
// A step plans (destination, window, raise), switches through Dock with its
// effect, then activates.
//
// Thread: event loop. A step waits for WindowServer queries, for Dock to
// switch (up to one second), for the snapshot capture and for an
// application's AX raise.
// State: the anchor, the Desktop the last step reached, which relative
// navigation starts from for a second while WindowServer's active display
// may still follow an application elsewhere; and when the last window fade
// ran.
// Callers: command runs steps and asks where navigation stands; the schedule
// asks for the time since the last click; the window fade uses the window and
// opacity rules. Calls topology, both effects, activation, the observed focus
// (window_focus_note) and the schedule's reports.

// One Desktop switch of a navigation. Its effect is either the fade of the
// destination's windows from `alpha`, or a crossfade of the whole display. A
// step that does not activate only switches and shows its effect: another
// step queued after it will. That step can find its Desktop current already,
// a jump over a whole lap of Desktops or the number of the Desktop reached;
// `settle` then still gives the Desktop's window focus.
struct space_navigation_step
{
    uint64_t sid;
    bool move;
    bool crossfade;
    float alpha;
    float duration;
    bool activate;
    bool settle;
};

// A step's decisions before Dock switches.
struct space_navigation_plan
{
    uint64_t current;
    uint64_t sid;
    uint32_t display;
    uint32_t current_display;
    uint32_t focus_id;          // The window to activate, 0 for none.
    bool move;
    bool activate;
    bool raise;
    float duration;
    uint64_t now;               // When the step was planned.
};

struct window;

static void space_navigation_forget(void);
static double space_navigation_seconds_since_click(void);
static uint64_t space_navigation_current_space(uint64_t active_sid);
static bool space_navigation_window(struct window *window);
static float space_navigation_opacity(struct window *window, uint32_t focused_id);
static bool space_navigation_run_step(uint64_t current, struct space_navigation_step *step);

#endif
