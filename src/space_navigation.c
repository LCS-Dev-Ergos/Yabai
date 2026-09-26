// Fork navigation: one event-loop request, no shell queries or global config edits.
static uint64_t space_navigation_last_time;

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

static bool space_navigation_run(uint64_t sid, bool move, float alpha, float duration)
{
    uint64_t current = space_manager_active_space();
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
            display_manager_focus_display(display, sid);
        }

        if (focus) {
            window_manager_focus_window_with_raise(&focus->application->psn, focus->id, focus->ref);
        }
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
