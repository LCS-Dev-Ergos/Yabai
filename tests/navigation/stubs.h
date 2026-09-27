static bool window_manager_is_window_eligible(struct window *w)
{
    return w->id != 0;
}

static bool window_check_flag(struct window *w, unsigned flag)
{
    return w->flags & flag;
}

static uint64_t space_manager_active_space(void)
{
    return active_space;
}

// Spaces 1 and 2 belong to display 1, 3 and 4 to display 2.
static uint32_t space_navigation_space_display(uint64_t sid)
{
    return sid >= 3 ? 2 : 1;
}

static uint64_t space_navigation_display_space(uint32_t display)
{
    return display == 2 ? (visible_space == 3 ? 3 : 4) : active_space;
}

static int space_navigation_spaces_visible_elsewhere(uint32_t display, uint64_t *list)
{
    if (single_display) return 0;

    list[0] = space_navigation_display_space(display == 1 ? 2 : 1);
    return 1;
}

static bool mission_control_is_active(void)
{
    return mission_control;
}

static bool display_manager_display_is_animating(uint32_t did)
{
    return animating;
}

static struct window *window_manager_focused_window(void *wm)
{
    return &windows[0];
}

static bool space_navigation_space_fullscreen(uint64_t sid)
{
    return fullscreen;
}

static void window_manager_send_window_to_space(void *sm, void *wm, struct window *w, uint64_t sid, bool rule)
{
    ++move_calls;
}

static uint32_t *space_window_list(uint64_t sid, int *count, bool minimized)
{
    *count = 2;
    return ids;
}

static struct window *window_manager_find_window(void *wm, uint32_t id)
{
    return id > 0 && id <= 2 ? &windows[id-1] : NULL;
}

static uint64_t read_os_timer(void)
{
    return timestamp;
}

static bool space_navigation_space_visible(uint64_t sid)
{
    return visible || (visible_space && sid == visible_space);
}

enum
{
    kCGEventSourceStateHIDSystemState,
    kCGEventLeftMouseDown,
    kCGEventRightMouseDown,
    kCGEventOtherMouseDown
};

static double CGEventSourceSecondsSinceLastEventType(int state, int type)
{
    return seconds_since_click;
}

static struct window **window_manager_find_application_windows(void *wm, struct application *application, int *count)
{
    static struct window *list[2];

    list[0] = &windows[0];
    list[1] = &windows[1];
    *count = single_window ? 1 : 2;

    return list;
}

// The second window is tiled in the view of other_window_space when
// other_window_tiled is set, and floating otherwise.
static struct view *window_manager_find_managed_window(void *wm, struct window *window)
{
    static struct view view;

    if (!other_window_tiled || window != &windows[1]) return NULL;

    view.sid = other_window_space;
    return &view;
}

// The application's windows on the given spaces. The second window may sit
// on a space of the other display.
static int space_navigation_spaces_windows(uint64_t *spaces, int space_count, int cid, uint32_t *ids, int capacity)
{
    int count = 0;

    ++window_list_queries;
    switches_at_query = focus_calls;
    assert(cid == app.connection && capacity >= 2);

    for (int i = 0; i < space_count; ++i) {
        if (spaces[i] == 1) ids[count++] = 1;
        if (other_window_space && spaces[i] == other_window_space) ids[count++] = 2;
    }

    return count;
}

static void window_focus_note(uint32_t id)
{
    noted_id = id;
}

static bool scripting_addition_set_opacity(uint32_t id, float alpha, float duration)
{
    assert(opacity_calls < 16);

    effects[opacity_calls].id = id;
    effects[opacity_calls].alpha = alpha;
    effects[opacity_calls].duration = duration;
    ++opacity_calls;

    return opacity_calls != opacity_fail_at;
}

static bool scripting_addition_focus_space(uint64_t sid)
{
    ++focus_calls;
    if (focus_success) active_space = sid;

    return focus_success;
}

static float space_navigation_frame_interval(uint32_t display)
{
    return 1.0f / 60.0f;
}

static bool space_navigation_reduce_motion(void)
{
    return reduce_motion;
}

static bool scripting_addition_set_opacity_batch(uint32_t display, uint8_t phase, float alpha, float duration,
                                                float interval, struct sa_window_opacity *windows, uint32_t count)
{
    ++batch_calls;
    bool success = true;
    for (uint32_t i = 0; i < count; ++i) {
        success = scripting_addition_set_opacity(windows[i].wid, windows[i].alpha, duration) && success;
    }

    return success;
}

static void display_manager_focus_display(uint32_t did, uint64_t sid)
{
    ++display_calls;
}

static void window_manager_focus_window_with_raise(int *psn, uint32_t id, void *ref)
{
    if (expect_fade_started) assert(opacity_calls == 4);
    ++window_focus_calls;
    ++raise_calls;
    focused_id = id;
}

static void space_navigation_focus_window(int *psn, uint32_t id)
{
    if (expect_fade_started) assert(opacity_calls == 4);
    ++window_focus_calls;
    focused_id = id;
}

static void space_navigation_focus_cancel(void)
{
    ++focus_cancels;
}

static void display_manager_set_active_display_id(uint32_t did)
{
    ++display_calls;
}

static void window_manager_center_mouse(void *wm, struct window *window)
{
}
