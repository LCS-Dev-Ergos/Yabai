// Display activation and configuration changes.
// Runs on the event-loop thread.

static EVENT_HANDLER(DISPLAY_CHANGED)
{
    uint32_t new_did = display_manager_active_display_id();
    if (g_display_manager.current_display_id == new_did) {
        debug("%s: newly activated display %d was already active (%d)! ignoring event..\n", __FUNCTION__, g_display_manager.current_display_id, new_did);
        return;
    }

    g_display_manager.last_display_id = g_display_manager.current_display_id;
    g_display_manager.current_display_id = new_did;

    g_space_manager.last_space_id = g_space_manager.current_space_id;
    g_space_manager.current_space_id = display_space_id(g_display_manager.current_display_id);

    uint32_t expected_display_id = space_display_id(g_space_manager.current_space_id);
    if (g_display_manager.current_display_id != expected_display_id) {
        debug("%s: %d %lld did not match %d! ignoring event..\n", __FUNCTION__, g_display_manager.current_display_id, g_space_manager.current_space_id, expected_display_id);
        return;
    }

    if (g_window_manager.menubar_opacity != 1.0f) {
        float alpha = space_is_fullscreen(g_space_manager.current_space_id) ? 1.0f : g_window_manager.menubar_opacity;
        SLSSetMenuBarInsetAndAlpha(g_connection, 0, 1, alpha);
    }

    debug("%s: %d %lld\n", __FUNCTION__, g_display_manager.current_display_id, g_space_manager.current_space_id);
    struct view *view = space_manager_find_view(&g_space_manager, g_space_manager.current_space_id);

    if (space_manager_refresh_application_windows(&g_space_manager)) {
        struct window *focused_window = window_manager_focused_window(&g_window_manager);
        if (focused_window && window_manager_find_lost_focused_event(&g_window_manager, focused_window->id)) {
            window_did_receive_focus(&g_window_manager, &g_mouse_state, focused_window);
            window_manager_remove_lost_focused_event(&g_window_manager, focused_window->id);
        }
    }

    if (!mission_control_is_active() && space_is_user(g_space_manager.current_space_id)) {
        window_manager_validate_and_check_for_windows_on_space(&g_space_manager, &g_window_manager, g_space_manager.current_space_id);

        if (view_is_invalid(view)) {
            view_update(view);
        }

        if (view_is_dirty(view)) {
            window_node_flush(view->root);
            view_clear_flag(view, VIEW_IS_DIRTY);
        }
    }

    event_signal_push(SIGNAL_DISPLAY_CHANGED, NULL);
}

static EVENT_HANDLER(DISPLAY_ADDED)
{
    space_navigation_snapshot_cancel();
    uint32_t did = (uint32_t)(intptr_t) context;
    debug("%s: %d\n", __FUNCTION__, did);
    space_manager_handle_display_add(&g_space_manager, did);
    window_manager_handle_display_add_and_remove(&g_space_manager, &g_window_manager, did);
    event_signal_push(SIGNAL_DISPLAY_ADDED, context);
}

static EVENT_HANDLER(DISPLAY_REMOVED)
{
    space_navigation_snapshot_cancel();
    uint32_t did = (uint32_t)(intptr_t) context;
    debug("%s: %d\n", __FUNCTION__, did);
    display_manager_remove_label_for_display(&g_display_manager, did);
    window_manager_handle_display_add_and_remove(&g_space_manager, &g_window_manager, display_manager_main_display_id());
    event_signal_push(SIGNAL_DISPLAY_REMOVED, context);
}

static EVENT_HANDLER(DISPLAY_MOVED)
{
    space_navigation_snapshot_cancel();
    uint32_t did = (uint32_t)(intptr_t) context;
    debug("%s: %d\n", __FUNCTION__, did);
    space_manager_mark_spaces_invalid(&g_space_manager);
    event_signal_push(SIGNAL_DISPLAY_MOVED, context);
}

static EVENT_HANDLER(DISPLAY_RESIZED)
{
    space_navigation_snapshot_cancel();
    uint32_t did = (uint32_t)(intptr_t) context;
    debug("%s: %d\n", __FUNCTION__, did);
    space_manager_mark_spaces_invalid_for_display(&g_space_manager, did);
    event_signal_push(SIGNAL_DISPLAY_RESIZED, context);
}
