// Dock, menu and wake events that affect window management.
// Runs on the event-loop thread.

static EVENT_HANDLER(DOCK_DID_RESTART)
{
    space_navigation_snapshot_cancel();
    debug("%s:\n", __FUNCTION__);

    if (workspace_is_macos_monterey() ||
        workspace_is_macos_ventura() ||
        workspace_is_macos_sonoma() ||
        workspace_is_macos_sequoia() ||
        workspace_is_macos_tahoe() ||
        workspace_is_macos_goldengate()) {
        mission_control_unobserve();
        mission_control_observe();
    }

    event_signal_push(SIGNAL_DOCK_DID_RESTART, NULL);
}

static enum ffm_mode ffm_value;
static int is_menu_open = 0;

static EVENT_HANDLER(MENU_OPENED)
{
    debug("%s\n", __FUNCTION__);
    ++is_menu_open;

    if (is_menu_open == 1) {
        ffm_value = g_window_manager.ffm_mode;
        g_window_manager.ffm_mode = FFM_DISABLED;
    }
}

static EVENT_HANDLER(MENU_CLOSED)
{
    debug("%s\n", __FUNCTION__);
    --is_menu_open;

    if (is_menu_open == 0) {
        g_window_manager.ffm_mode = ffm_value;
    } else if (is_menu_open < 0) {
        is_menu_open = 0;
    }
}

static EVENT_HANDLER(MENU_BAR_HIDDEN_CHANGED)
{
    debug("%s:\n", __FUNCTION__);
    space_manager_mark_spaces_invalid(&g_space_manager);
    event_signal_push(SIGNAL_MENU_BAR_HIDDEN_CHANGED, NULL);
}

static EVENT_HANDLER(DOCK_DID_CHANGE_PREF)
{
    debug("%s:\n", __FUNCTION__);
    space_manager_mark_spaces_invalid(&g_space_manager);
    event_signal_push(SIGNAL_DOCK_DID_CHANGE_PREF, NULL);
}

static EVENT_HANDLER(SYSTEM_WOKE)
{
    space_navigation_snapshot_cancel();
    debug("%s:\n", __FUNCTION__);

    struct window *focused_window = window_manager_find_window(&g_window_manager, g_window_manager.focused_window_id);
    if (focused_window) {
        window_manager_set_window_opacity(&g_window_manager, focused_window, g_window_manager.active_window_opacity);
        window_manager_center_mouse(&g_window_manager, focused_window);
    }

    event_signal_push(SIGNAL_SYSTEM_WOKE, NULL);
}
