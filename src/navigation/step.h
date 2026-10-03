#ifndef NAVIGATION_STEP_H
#define NAVIGATION_STEP_H

#include "../core_types.h"

// Navigation step (step.c): one Desktop switch with its effect and, on the last
// step, the activation of a window.
//
// A step plans (destination, window, raise), switches through Dock with its effect,
// then activates. A queued crossfade step is split in two: planning requests the
// capture and returns, and the capture's event (SPACE_NAVIGATION_CAPTURED, a hook,
// see hooks.h) switches and activates. A veil step captures nothing: it shows the
// veil, switches and activates in one run, like a window fade.
//
// Thread: event loop. A step waits for WindowServer queries, for Dock to switch (up
// to one second), for an application's AX raise and, when run to its end at once,
// for the snapshot capture or the veil's three refreshes.
// State: the anchor, the Desktop the last step reached, which relative navigation
// starts from for a second while WindowServer's active display may still follow an
// application elsewhere; when the last window fade ran; and the step waiting for its
// capture (space_navigation_flight).
// Callers: command runs steps, begins queued ones, cancels the one in flight and
// asks where navigation stands; the schedule asks for the time since the last click;
// the window fade uses the window and opacity rules. Calls topology, both effects,
// activation, the observed focus (window_focus_note) and the schedule, which says
// whether steps queued since a step started take its activation over and hears its
// reports, including the end of a step in flight.

// The overlay a request without its own effect shows (config
// navigation_effect_type).
enum space_navigation_effect_type
{
    SPACE_NAVIGATION_EFFECT_CROSSFADE,
    SPACE_NAVIGATION_EFFECT_VEIL,
    SPACE_NAVIGATION_EFFECT_COUNT
};

static char *space_navigation_effect_type_str[] =
{
    [SPACE_NAVIGATION_EFFECT_CROSSFADE] = "crossfade",
    [SPACE_NAVIGATION_EFFECT_VEIL]      = "veil"
};

// What a crossfade becomes while macOS reports memory pressure (config
// navigation_pressure_fallback): the veil, which captures nothing; the crossfade all
// the same; or Dock's switch alone.
enum space_navigation_pressure_fallback
{
    SPACE_NAVIGATION_PRESSURE_VEIL,
    SPACE_NAVIGATION_PRESSURE_KEEP,
    SPACE_NAVIGATION_PRESSURE_NONE,
    SPACE_NAVIGATION_PRESSURE_COUNT
};

static char *space_navigation_pressure_fallback_str[] =
{
    [SPACE_NAVIGATION_PRESSURE_VEIL] = "veil",
    [SPACE_NAVIGATION_PRESSURE_KEEP] = "keep",
    [SPACE_NAVIGATION_PRESSURE_NONE] = "none"
};

// One Desktop switch of a navigation. Its effect is the fade of the destination's
// windows from `alpha`, a crossfade of the whole display, or a veil over it;
// `crossfade` and `veil` exclude each other. A step that does not activate only
// switches and shows its effect: another step queued after it will. That step can
// find its Desktop current already, a jump over a whole lap of Desktops or the
// number of the Desktop reached; `settle` then still gives the Desktop's window
// focus. The daemon's settings apply when the step starts: with navigation_effect
// off it shows no effect, and a crossfade under memory pressure follows
// navigation_pressure_fallback.
struct space_navigation_step
{
    uint64_t sid;
    bool move;
    bool crossfade;
    bool veil;
    float alpha;
    float duration;
    bool activate;
    bool settle;
};

// A step's decisions before Dock switches, which an asynchronous capture carries to
// the switch.
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

// How a step started by space_navigation_begin_step stands when it returns.
enum space_navigation_result
{
    SPACE_NAVIGATION_FAILED,
    SPACE_NAVIGATION_SWITCHED,
    SPACE_NAVIGATION_PENDING    // Waits for its capture; the schedule hears later.
};

struct window;

static void space_navigation_forget(void);
static double space_navigation_seconds_since_click(void);
static uint64_t space_navigation_current_space(uint64_t active_sid);
static bool space_navigation_window(struct window *window);
static float space_navigation_opacity(struct window *window, uint32_t focused_id);
static bool space_navigation_run_step(uint64_t current, struct space_navigation_step *step);
static enum space_navigation_result space_navigation_begin_step(uint64_t current, struct space_navigation_step *step);
// A quick accepted request finishes a pending focus step without its snapshot. Moves
// keep their effect. This does not cancel logical navigation.
static void space_navigation_step_skip_effect(void);
static void space_navigation_step_cancel(void);
// An accepted focus request after a click retires a capture from before it.
static bool space_navigation_step_replace_after_click(uint64_t input_time);

#endif
