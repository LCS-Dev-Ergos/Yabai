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
    if (age >= SPACE_NAVIGATION_ANCHOR_NS || !space_is_visible(sid)) return active_sid;
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

// An application activated with another window visible on a different
// display, such as a Chromium browser, can make that window key instead.
static bool space_navigation_needs_raise(struct window *window, uint32_t display)
{
    int count = 0;
    struct window **list = window_manager_find_application_windows(&g_window_manager, window->application, &count);

    for (int i = 0; i < count; ++i) {
        struct window *other = list[i];
        if (other == window || !space_navigation_window(other)) continue;

        uint64_t sid = window_space(other->id);
        if (sid && space_is_visible(sid) && space_display_id(sid) != display) return true;
    }

    return false;
}

static bool space_navigation_run(uint64_t current, uint64_t sid, bool move, float alpha, float duration)
{
    if (current == sid) return true;

    uint32_t display = space_display_id(sid);
    if (mission_control_is_active() || display_manager_display_is_animating(display)) return false;

    struct window *focus = NULL;

    if (move) {
        focus = window_manager_focused_window(&g_window_manager);

        if (!space_navigation_window(focus) || space_is_fullscreen(sid)
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

    uint64_t now = read_os_timer();

    // Repeated navigation stays immediate. Suppress effects, never navigation.
    bool fade = duration > 0.0f && alpha < 1.0f && !space_is_visible(sid)
        && !space_is_fullscreen(sid)
        && (space_navigation_last_time == 0 || now - space_navigation_last_time >= 180000000ULL);

    uint32_t focus_id = focus ? focus->id : 0;
    bool effects_ok = true;

    if (fade) {
        for (int i = 0; i < count; ++i) {
            struct window *window = window_manager_find_window(&g_window_manager, ids[i]);
            if (!space_navigation_window(window)) continue;

            float end = space_navigation_opacity(window, focus_id);
            if (alpha < end) {
                effects_ok = scripting_addition_set_opacity(window->id, alpha, 0.0f) && effects_ok;
            }
        }
    }

    // Unlike generic --focus, do not silently fall back to asynchronous gestures
    // after dimming windows: failure must restore opacity before returning.
    bool success = effects_ok && scripting_addition_focus_space(sid);

    if (success) {
        space_navigation_last_time = now;

        if (space_display_id(current) != display) {
            if (focus) {
                display_manager_set_active_display_id(display);
                window_manager_center_mouse(&g_window_manager, focus);
            } else {
                display_manager_focus_display(display, sid);
            }
        }

        if (focus) {
            if (move || space_navigation_needs_raise(focus, display)) {
                // Raising completes before the next navigation can switch
                // away, so the application cannot raise it on a hidden space.
                window_manager_focus_window_with_raise(&focus->application->psn, focus->id, focus->ref);
            } else {
                // The selected window is already frontmost. AXRaise can block
                // while its application responds to the space switch.
                window_manager_focus_window_without_raise(&focus->application->psn, focus->id);
            }

            // The activation that follows need not ask the application,
            // which is busy with the switch, for its focused window.
            window_focus_note(focus->id);
        }

        space_navigation_anchor.sid = sid;
        space_navigation_anchor.time = read_os_timer();
    }

    if (fade) {
        for (int i = 0; i < count; ++i) {
            struct window *window = window_manager_find_window(&g_window_manager, ids[i]);
            if (!space_navigation_window(window)) continue;

            float end = space_navigation_opacity(window, success ? focus_id : g_window_manager.focused_window_id);
            if (alpha < space_navigation_opacity(window, focus_id)) {
                effects_ok = scripting_addition_set_opacity(window->id, end, success ? duration : 0.0f) && effects_ok;
            }
        }
    }

    return success && effects_ok;
}
