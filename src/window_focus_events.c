static void window_focus_note(uint32_t window_id)
{
    __atomic_store_n(&__pending_window_focus_id, window_id, __ATOMIC_RELEASE);
}

static void window_focus_consume(uint32_t window_id)
{
    // Handling an older notification must not erase a newer observation.
    __atomic_compare_exchange_n(&__pending_window_focus_id, &window_id, 0,
                                false, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED);
}

static uint32_t window_focus_observed_id(struct application *application)
{
    uint32_t window_id = __atomic_load_n(&__pending_window_focus_id, __ATOMIC_ACQUIRE);
    struct window *window = window_manager_find_window(&g_window_manager, window_id);

    if (window && window->application == application
        && __atomic_load_n(&window->id_ptr, __ATOMIC_ACQUIRE) == &window->id
        && !window_check_flag(window, WINDOW_MINIMIZE)
        && space_is_visible(window_space(window->id))) {
        return window_id;
    }

    return application_focused_window(application);
}

static void window_manager_handle_front_focus(struct application *application)
{
    // Activation signals were already delivered. A stale event must not
    // query or apply the focus of an application that is now in the background.
    if (!application_is_frontmost(application)) return;

    uint32_t application_focused_window_id = window_focus_observed_id(application);
    if (!application_focused_window_id) {
        struct window *focused_window = window_manager_find_window(&g_window_manager, g_window_manager.focused_window_id);
        if (focused_window) {
            window_manager_set_window_opacity(&g_window_manager, focused_window, g_window_manager.normal_window_opacity);
        }

        g_window_manager.last_window_id = g_window_manager.focused_window_id;
        g_window_manager.focused_window_id = 0;
        g_window_manager.focused_window_psn = application->psn;
        g_mouse_state.ffm_window_id = 0;
        return;
    }

    struct window *window = window_manager_find_window(&g_window_manager, application_focused_window_id);
    if (!window) {
        struct window *focused_window = window_manager_find_window(&g_window_manager, g_window_manager.focused_window_id);
        if (focused_window) {
            window_manager_set_window_opacity(&g_window_manager, focused_window, g_window_manager.normal_window_opacity);
        }

        window_manager_add_lost_focused_event(&g_window_manager, application_focused_window_id);
        return;
    }

    window_did_receive_focus(&g_window_manager, &g_mouse_state, window);
    event_signal_push(SIGNAL_WINDOW_FOCUSED, window);
    __atomic_store_n(&__pending_window_focus, false, __ATOMIC_RELEASE);
}
