// Fork navigation: one event-loop request, no shell queries or global config edits.
static uint64_t space_navigation_last_time;

// The space the last navigation switched to. For a moment after a switch,
// WindowServer's active display can follow an application to a window it has
// on another display; relative navigation starts from this space meanwhile.
#define SPACE_NAVIGATION_ANCHOR_NS 1000000000ULL

static struct
{
    uint64_t sid;
    uint64_t time;
} space_navigation_anchor;

static void space_navigation_forget(void)
{
    space_navigation_anchor.sid = 0;
}

static double space_navigation_seconds_since_click(void)
{
    double left  = CGEventSourceSecondsSinceLastEventType(kCGEventSourceStateHIDSystemState, kCGEventLeftMouseDown);
    double right = CGEventSourceSecondsSinceLastEventType(kCGEventSourceStateHIDSystemState, kCGEventRightMouseDown);
    double other = CGEventSourceSecondsSinceLastEventType(kCGEventSourceStateHIDSystemState, kCGEventOtherMouseDown);

    return fmin(left, fmin(right, other));
}

// Past the anchor's lifetime, or after a click, the active display reflects
// where the user works again.
static uint64_t space_navigation_current_space(uint64_t active_sid)
{
    uint64_t sid = space_navigation_anchor.sid;
    if (!sid || sid == active_sid) return active_sid;

    uint64_t age = read_os_timer() - space_navigation_anchor.time;
    if (age >= SPACE_NAVIGATION_ANCHOR_NS || !space_navigation_space_visible(sid)) return active_sid;
    if (space_navigation_seconds_since_click() * 1e9 < (double) age) return active_sid;

    return sid;
}

// The space `steps` places after the space at `index` (1-based) of `count`,
// wrapping around at both ends.
static int space_navigation_step_index(int index, int count, int steps)
{
    int result = (index - 1 + steps) % count;
    if (result < 0) result += count;

    return result + 1;
}

static bool space_navigation_window(struct window *window)
{
    return window && window_manager_is_window_eligible(window)
        && !window->application->is_hidden
        && !window_check_flag(window, WINDOW_MINIMIZE)
        && !window_check_flag(window, WINDOW_STICKY);
}

static float space_navigation_opacity(struct window *window, uint32_t focused_id)
{
    if (window->opacity != 0.0f) return window->opacity;
    if (!g_window_manager.enable_window_opacity) return 1.0f;

    return window->id == focused_id ? g_window_manager.active_window_opacity
                                    : g_window_manager.normal_window_opacity;
}

static bool space_navigation_spaces_contain(uint64_t *spaces, int count, uint64_t sid)
{
    for (int i = 0; i < count; ++i) {
        if (spaces[i] == sid) return true;
    }

    return false;
}

// An application activated with another window visible on a different
// display, such as a Chromium browser, can make that window key instead.
// A tiled window's Desktop is the Space of its view. Only floating and
// unmanaged windows need WindowServer, which lists the application's windows
// on the Desktops visible elsewhere with one query.
static bool space_navigation_needs_raise(struct window *window, uint32_t display)
{
    uint64_t spaces[SPACE_NAVIGATION_DISPLAYS_MAX];
    int space_count = space_navigation_spaces_visible_elsewhere(display, spaces);
    if (!space_count) return false;

    int count = 0;
    struct window **list = window_manager_find_application_windows(&g_window_manager, window->application, &count);

    bool unknown = false;
    for (int i = 0; i < count; ++i) {
        if (list[i] == window || !space_navigation_window(list[i])) continue;

        struct view *view = window_manager_find_managed_window(&g_window_manager, list[i]);
        if (!view) {
            unknown = true;
        } else if (space_navigation_spaces_contain(spaces, space_count, view->sid)) {
            return true;
        }
    }

    if (!unknown) return false;

    uint32_t ids[SPACE_NAVIGATION_WINDOWS_MAX];
    int id_count = space_navigation_spaces_windows(spaces, space_count, window->application->connection,
                                                   ids, SPACE_NAVIGATION_WINDOWS_MAX);

    for (int i = 0; i < id_count; ++i) {
        struct window *other = window_manager_find_window(&g_window_manager, ids[i]);
        if (other == window || !space_navigation_window(other)) continue;

        if (other->application == window->application) return true;
    }

    return false;
}

#include "space_navigation_effects.c"

// One Desktop switch of a navigation. Its effect is either the fade of the
// destination's windows from `alpha`, or a crossfade of the whole display. A
// step that does not activate only switches and shows its effect: another
// step queued after it will.
struct space_navigation_step
{
    uint64_t sid;
    bool move;
    bool crossfade;
    float alpha;
    float duration;
    bool activate;
};

static void space_navigation_schedule_activated(uint32_t window_id);
static void space_navigation_schedule_switched(float duration);

static bool space_navigation_run_step(uint64_t current, struct space_navigation_step *step)
{
    uint64_t sid = step->sid;
    bool move = step->move;
    float alpha = step->alpha;
    float duration = step->duration;

    if (current == sid) return true;

    // A deferred focus from the previous navigation must not follow this one.
    space_navigation_focus_cancel();

    uint32_t display = space_navigation_space_display(sid);
    if (mission_control_is_active() || display_manager_display_is_animating(display)) return false;

    struct window *focus = NULL;

    if (move) {
        focus = window_manager_focused_window(&g_window_manager);

        if (!space_navigation_window(focus) || space_navigation_space_fullscreen(sid)
            || window_check_flag(focus, WINDOW_FULLSCREEN)) {
            return false;
        }

        window_manager_send_window_to_space(&g_space_manager, &g_window_manager, focus, sid, false);
    }

    int count = 0;
    uint32_t *ids = space_window_list(sid, &count, false);
    if (!ids) count = 0;

    for (int i = 0; !focus && i < count; ++i) {
        struct window *window = window_manager_find_window(&g_window_manager, ids[i]);
        if (space_navigation_window(window)) focus = window;
    }

    // Decided before the switch: afterwards WindowServer is busy showing the
    // new Desktop, and each query waits for about a frame.
    bool raise = focus && step->activate && (move || space_navigation_needs_raise(focus, display));

    uint64_t now = read_os_timer();

    // The crossfade blends two opaque Desktops, so it stays under Reduce
    // Motion, whose own Desktop transition is a crossfade. It needs a hidden
    // destination and ordinary Desktops on both sides.
    bool crossfade = step->crossfade && duration > 0.0f && !space_navigation_space_visible(sid)
        && !space_navigation_space_fullscreen(sid) && !space_navigation_space_fullscreen(current);

    // Repeated navigation stays immediate. Suppress effects, never navigation.
    bool fade = !step->crossfade && duration > 0.0f && alpha < 1.0f && !space_navigation_space_visible(sid)
        && !space_navigation_space_fullscreen(sid)
        && (space_navigation_last_time == 0 || now - space_navigation_last_time >= 180000000ULL)
        && !space_navigation_reduce_motion();

    uint32_t focus_id = focus ? focus->id : 0;
    struct space_navigation_effect effect = { .count = 0 };
    bool effects_ok = step->crossfade || space_navigation_prepare_effect(&effect, display, ids, count, focus_id, alpha, fade);
    bool success;
    bool crossfade_started = false;

    if (crossfade) {
        // A crossfade that Dock refuses leaves the Desktop as it was: switch
        // without an effect.
        float interval = space_navigation_frame_interval(display);
        crossfade_started = scripting_addition_focus_space_crossfade(display, sid, duration, interval);
        success = crossfade_started || scripting_addition_focus_space(sid);
    } else if (step->crossfade) {
        success = scripting_addition_focus_space(sid);
    } else {
        // Unlike generic --focus, do not silently fall back to asynchronous
        // gestures after dimming windows: failure must restore opacity
        // before returning.
        success = effects_ok && scripting_addition_focus_space(sid);

        // Start the entire group before application activation or AXRaise
        // can block. Failure restores ordinary opacity without leaving dim
        // windows.
        effects_ok = space_navigation_start_effect(&effect, display,
            success ? focus_id : g_window_manager.focused_window_id, alpha, duration, success) && effects_ok;
    }

    if (success) {
        space_navigation_last_time = now;
        space_navigation_schedule_switched(crossfade_started ? duration : 0.0f);
    }

    if (success && step->activate) {
        if (space_navigation_space_display(current) != display) {
            if (focus) {
                display_manager_set_active_display_id(display);
                window_manager_center_mouse(&g_window_manager, focus);
            } else {
                display_manager_focus_display(display, sid);
            }
        }

        if (focus) {
            if (move) {
                window_manager_focus_window_with_raise(&focus->application->psn, focus->id, focus->ref);
            } else if (raise) {
                // Raising completes before the next navigation can switch
                // away, so the application cannot raise it on a hidden space.
                space_navigation_raise_window(&focus->application->psn, focus->id, focus->ref);
            } else {
                // The selected window is already frontmost. AXRaise can block
                // while its application responds to the space switch.
                space_navigation_focus_window(&focus->application->psn, focus->id);
            }

            // The activation that follows need not ask the application,
            // which is busy with the switch, for its focused window.
            window_focus_note(focus->id);
            space_navigation_schedule_activated(focus->id);
        }
    }

    if (success) {
        space_navigation_anchor.sid = sid;
        space_navigation_anchor.time = read_os_timer();
    }

    return success && effects_ok;
}
