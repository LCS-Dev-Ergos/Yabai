// Space creation, removal and current-Space changes.
// Runs on the event-loop thread.
#ifdef YABAI_CAPTURE_DIAGNOSTICS
#include <os/signpost.h>
static void space_changed_diag(const char *phase, uint64_t sid)
{
    static os_log_t log;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ log = os_log_create("com.lcs.yabai", "effects"); });
    os_signpost_event_emit(log, OS_SIGNPOST_ID_EXCLUSIVE, "diag",
                           "%{public}s sid %llu ns %llu", phase,
                           (unsigned long long)sid,
                           (unsigned long long)clock_gettime_nsec_np(CLOCK_UPTIME_RAW));
}
#define SPACE_DIAG(phase) space_changed_diag(phase, g_space_manager.current_space_id)
#else
#define SPACE_DIAG(phase) ((void)0)
#endif

static EVENT_HANDLER(SLS_SPACE_CREATED)
{
    uint64_t sid = (uint64_t)(intptr_t) context;
    // The crossfade's or veil's own Space, see effects/snapshot.m.
    if (space_navigation_snapshot_owns_space(sid)) return;

    int type = SLSSpaceGetType(g_connection, sid);

    if (type == 0 || type == 4) {
        debug("%s: %lld, %d\n", __FUNCTION__, sid, type);
        space_manager_find_view(&g_space_manager, sid);
        event_signal_push(SIGNAL_SPACE_CREATED, context);
    }
}

static EVENT_HANDLER(SLS_SPACE_DESTROYED)
{
    uint64_t sid = (uint64_t)(intptr_t) context;
    struct view *view = table_find(&g_space_manager.view, &sid);
    if (view) {
        debug("%s: %lld\n", __FUNCTION__, sid);
        space_manager_remove_label_for_space(&g_space_manager, sid);
        table_remove(&g_space_manager.view, &sid);
        view_destroy(view);
        free(view);
        event_signal_push(SIGNAL_SPACE_DESTROYED, context);
    }
}

static EVENT_HANDLER(SPACE_CHANGED)
{
    SPACE_DIAG("space_changed_begin");
    space_navigation_snapshot_space_changed();
    g_space_manager.last_space_id = g_space_manager.current_space_id;
    // The space notification describes WindowServer state. Asking the front
    // application's AX window here can stall the event loop during a switch.
    g_space_manager.current_space_id = display_space_id(display_manager_active_display_id());

    if (g_window_manager.menubar_opacity != 1.0f) {
        float alpha = space_is_fullscreen(g_space_manager.current_space_id) ? 1.0f : g_window_manager.menubar_opacity;
        SLSSetMenuBarInsetAndAlpha(g_connection, 0, 1, alpha);
    }

    debug("%s: %lld\n", __FUNCTION__, g_space_manager.current_space_id);
    struct view *view = space_manager_find_view(&g_space_manager, g_space_manager.current_space_id);
    SPACE_DIAG("space_changed_lookup_end");

    if (space_manager_refresh_application_windows(&g_space_manager)) {
        struct window *focused_window = window_manager_focused_window(&g_window_manager);
        if (focused_window && window_manager_find_lost_focused_event(&g_window_manager, focused_window->id)) {
            window_did_receive_focus(&g_window_manager, &g_mouse_state, focused_window);
            window_manager_remove_lost_focused_event(&g_window_manager, focused_window->id);
        }
    }
    SPACE_DIAG("space_changed_refresh_end");

    if (!mission_control_is_active() && space_is_user(g_space_manager.current_space_id)) {
        window_manager_validate_and_check_for_windows_on_space(&g_space_manager, &g_window_manager, g_space_manager.current_space_id);
        SPACE_DIAG("space_changed_validate_end");

        if (view_is_invalid(view)) {
            view_update(view);
        }
        SPACE_DIAG("space_changed_update_end");

        if (view_is_dirty(view)) {
            window_node_flush(view->root);
            view_clear_flag(view, VIEW_IS_DIRTY);
        }
        SPACE_DIAG("space_changed_flush_end");
    }

    SPACE_DIAG("space_changed_end");

    event_signal_push(SIGNAL_SPACE_CHANGED, NULL);
}
