// Window lifecycle, focus, geometry and WindowServer notifications.
// Runs on the event-loop thread.

static EVENT_HANDLER(WINDOW_CREATED)
{
    uint32_t window_id = ax_window_id(context);
    if (!window_id) { CFRelease(context); return; }

    struct window *existing_window = window_manager_find_window(&g_window_manager, window_id);
    if (existing_window) { CFRelease(context); return; }

    pid_t window_pid = ax_window_pid(context);
    if (!window_pid) { CFRelease(context); return; }

    struct application *application = window_manager_find_application(&g_window_manager, window_pid);
    if (!application) { CFRelease(context); return; }

    struct window *window = window_manager_create_and_add_window(&g_space_manager, &g_window_manager, application, context, window_id, true);
    if (!window) return;

    int rule_len = buf_len(g_window_manager.rules);
    for (int i = 0; i < rule_len; ++i) {
        if (rule_check_flag(&g_window_manager.rules[i], RULE_ONE_SHOT_REMOVE)) {
            rule_destroy(&g_window_manager.rules[i]);
            if (buf_del(g_window_manager.rules, i)) {
                --i;
                --rule_len;
            }
        }
    }

    if (window_manager_should_manage_window(window) && !window_manager_find_managed_window(&g_window_manager, window)) {
        uint64_t sid;

        if (g_window_manager.window_origin_mode == WINDOW_ORIGIN_DEFAULT) {
            sid = window_space(window->id);
        } else if (g_window_manager.window_origin_mode == WINDOW_ORIGIN_FOCUSED) {
            sid = g_space_manager.current_space_id;
        } else /* if (g_window_manager.window_origin_mode == WINDOW_ORIGIN_CURSOR) */ {
            sid = space_manager_cursor_space();
        }

        struct view *view = space_manager_tile_window_on_space(&g_space_manager, window, sid);
        window_manager_add_managed_window(&g_window_manager, window, view);
    }

    if (window_manager_is_window_eligible(window)) {
        event_signal_push(SIGNAL_WINDOW_CREATED, window);
    }

    if (workspace_is_macos_sequoia() || workspace_is_macos_tahoe() || workspace_is_macos_goldengate()) {
        update_window_notifications();
    }
}

static EVENT_HANDLER(WINDOW_DESTROYED)
{
    struct window *window = context;
    if (!window || window->id == 0) {
        debug("%s: window has already been destroyed, ignoring event..\n", __FUNCTION__);
        return;
    }

    debug("%s: %s %d\n", __FUNCTION__, window->application ? window->application->name : "<unknown>", window->id);

    struct view *view = window_manager_find_managed_window(&g_window_manager, window);
    if (view) {
        space_manager_untile_window(view, window);
        window_manager_remove_managed_window(&g_window_manager, window->id);
    }

    if (g_mouse_state.window == window) g_mouse_state.window = NULL;
    if (g_mouse_state.ffm_window_id == window->id) g_mouse_state.ffm_window_id = 0;

    if (window->is_eligible) {
        event_signal_push(SIGNAL_WINDOW_DESTROYED, window);
    }

    window_manager_remove_scratchpad_for_window(&g_window_manager, window, false);
    window_manager_remove_window(&g_window_manager, window->id);
    window_unobserve(window);
    window_destroy(window);

    if (workspace_is_macos_sequoia() || workspace_is_macos_tahoe() || workspace_is_macos_goldengate()) {
        update_window_notifications();
    }
}

static EVENT_HANDLER(WINDOW_FOCUSED)
{
    __atomic_store_n(&__pending_window_focus, false, __ATOMIC_RELEASE);
    uint32_t window_id = (uint32_t)(intptr_t) context;
    window_focus_consume(window_id);
    space_navigation_schedule_focused(window_id);

    struct window *window = window_manager_find_window(&g_window_manager, window_id);
    if (!window) {
        window_manager_add_lost_focused_event(&g_window_manager, window_id);
        return;
    }

    if (!__sync_bool_compare_and_swap(&window->id_ptr, &window->id, &window->id)) {
        debug("%s: %d has been marked invalid by the system, ignoring event..\n", __FUNCTION__, window_id);
        return;
    }

    if (window_check_flag(window, WINDOW_MINIMIZE)) {
        window_manager_add_lost_focused_event(&g_window_manager, window->id);
        return;
    }

    if (!application_is_frontmost(window->application)) {
        return;
    }

    debug("%s: %s %d\n", __FUNCTION__, window->application->name, window->id);

    if (g_space_manager.skip_window_focus_animation) {
        uint64_t sid = window_space(window->id);
        if (sid && !space_is_visible(sid)) {
            SLSSpaceSetFrontPSN(g_connection, sid, window->application->psn);
            space_manager_focus_space_using_gesture(space_display_id(sid), sid);
        }
    }

    window_did_receive_focus(&g_window_manager, &g_mouse_state, window);
    event_signal_push(SIGNAL_WINDOW_FOCUSED, window);
}

static EVENT_HANDLER(WINDOW_MOVED)
{
    uint32_t window_id = (uint32_t)(intptr_t) context;
    struct window *window = window_manager_find_window(&g_window_manager, window_id);
    if (!window) return;

    if (!__sync_bool_compare_and_swap(&window->id_ptr, &window->id, &window->id)) {
        debug("%s: %d has been marked invalid by the system, ignoring event..\n", __FUNCTION__, window_id);
        return;
    }

    if (window->application->is_hidden) {
        debug("%s: %d was moved while the application is hidden, ignoring event..\n", __FUNCTION__, window_id);
        return;
    }

    CGPoint new_origin = window_ax_origin(window);
    if (CGPointEqualToPoint(new_origin, window->frame.origin)) {
        debug("%s:DEBOUNCED %s %d\n", __FUNCTION__, window->application->name, window->id);
        return;
    }

    debug("%s: %s %d\n", __FUNCTION__, window->application->name, window->id);
    event_signal_push(SIGNAL_WINDOW_MOVED, window);
    bool windowed_fullscreen = CGRectEqualToRect(window->windowed_frame, window->frame);
    window->frame.origin = new_origin;

    if (!windowed_fullscreen) {
        window_clear_flag(window, WINDOW_WINDOWED);

        if (!g_mouse_state.window || g_mouse_state.window != window) {
            struct view *view = window_manager_find_managed_window(&g_window_manager, window);
            if (view) {
                struct window_node *node = view_find_window_node(view, window->id);
                if (node && (AX_DIFF(node->area.x, new_origin.x) ||
                             AX_DIFF(node->area.y, new_origin.y))
                         &&
                   (!node->zoom || AX_DIFF(node->zoom->area.x, new_origin.x) ||
                                   AX_DIFF(node->zoom->area.y, new_origin.y))) {
                    if (space_is_visible(view->sid)) {
                        window_node_flush(node);
                    } else {
                        view_set_flag(view, VIEW_IS_DIRTY);
                    }
                }
            }
        }
    }
}

static EVENT_HANDLER(WINDOW_RESIZED)
{
    uint32_t window_id = (uint32_t)(intptr_t) context;
    struct window *window = window_manager_find_window(&g_window_manager, window_id);
    if (!window) return;

    if (!__sync_bool_compare_and_swap(&window->id_ptr, &window->id, &window->id)) {
        debug("%s: %d has been marked invalid by the system, ignoring event..\n", __FUNCTION__, window_id);
        return;
    }

    if (window->application->is_hidden) {
        debug("%s: %d was resized while the application is hidden, ignoring event..\n", __FUNCTION__, window_id);
        return;
    }

    CGRect new_frame = window_ax_frame(window);
    if (CGRectEqualToRect(new_frame, window->frame)) {
        debug("%s:DEBOUNCED %s %d\n", __FUNCTION__, window->application->name, window->id);
        return;
    }

    debug("%s: %s %d\n", __FUNCTION__, window->application->name, window->id);
    event_signal_push(SIGNAL_WINDOW_RESIZED, window);

    bool was_fullscreen = window_check_flag(window, WINDOW_FULLSCREEN);

    bool is_fullscreen = window_is_fullscreen(window);
    if (is_fullscreen) {
        window_set_flag(window, WINDOW_FULLSCREEN);
    } else {
        window_clear_flag(window, WINDOW_FULLSCREEN);
    }

    if (was_fullscreen != is_fullscreen) {
        if (window_ax_can_move(window)) {
            window_set_flag(window, WINDOW_MOVABLE);
        } else {
            window_clear_flag(window, WINDOW_MOVABLE);
        }

        if (window_ax_can_resize(window)) {
            window_set_flag(window, WINDOW_RESIZABLE);
        } else {
            window_clear_flag(window, WINDOW_RESIZABLE);
        }

        if (window->role) CFRelease(window->role);
        window->role = window_ax_role(window);

        if (window->subrole) CFRelease(window->subrole);
        window->subrole = window_ax_subrole(window);
    }

    bool windowed_fullscreen = CGRectEqualToRect(window->windowed_frame, window->frame);
    window->frame = new_frame;

    if (!was_fullscreen && is_fullscreen) {
        struct view *view = window_manager_find_managed_window(&g_window_manager, window);
        if (view) {
            space_manager_untile_window(view, window);
            window_manager_remove_managed_window(&g_window_manager, window->id);
            window_manager_purify_window(&g_window_manager, window);
        }
    } else if (was_fullscreen && !is_fullscreen) {
        window_manager_wait_for_native_fullscreen_transition(window);

        if (window_manager_should_manage_window(window) && !window_manager_find_managed_window(&g_window_manager, window)) {
            struct view *view = space_manager_tile_window_on_space(&g_space_manager, window, window_space(window->id));
            window_manager_add_managed_window(&g_window_manager, window, view);
        }
    } else if (!was_fullscreen == !is_fullscreen) {
        if (g_mouse_state.current_action == MOUSE_MODE_MOVE && g_mouse_state.window == window) {
            g_mouse_state.window_frame.size = g_mouse_state.window->frame.size;
        }

        if (!windowed_fullscreen) {
            window_clear_flag(window, WINDOW_WINDOWED);

            if (!g_mouse_state.window || g_mouse_state.window != window) {
                struct view *view = window_manager_find_managed_window(&g_window_manager, window);
                if (view) {
                    struct window_node *node = view_find_window_node(view, window->id);
                    if (node && (AX_DIFF(node->area.x, new_frame.origin.x)   ||
                                 AX_DIFF(node->area.y, new_frame.origin.y)   ||
                                 AX_DIFF(node->area.w, new_frame.size.width) ||
                                 AX_DIFF(node->area.h, new_frame.size.height))
                             &&
                       (!node->zoom || AX_DIFF(node->zoom->area.x, new_frame.origin.x)   ||
                                       AX_DIFF(node->zoom->area.y, new_frame.origin.y)   ||
                                       AX_DIFF(node->zoom->area.w, new_frame.size.width) ||
                                       AX_DIFF(node->zoom->area.h, new_frame.size.height))) {
                        if (space_is_visible(view->sid)) {
                            window_node_flush(node);
                        } else {
                            view_set_flag(view, VIEW_IS_DIRTY);
                        }
                    }
                }
            }
        }
    }
}

static EVENT_HANDLER(WINDOW_MINIMIZED)
{
    struct window *window = context;

    if (!__sync_bool_compare_and_swap(&window->id_ptr, &window->id, &window->id)) {
        debug("%s: %d has been marked invalid by the system, ignoring event..\n", __FUNCTION__, window->id);
        return;
    }

    debug("%s: %s %d\n", __FUNCTION__, window->application->name, window->id);
    window_set_flag(window, WINDOW_MINIMIZE);

    if (window_ax_can_move(window)) {
        window_set_flag(window, WINDOW_MOVABLE);
    } else {
        window_clear_flag(window, WINDOW_MOVABLE);
    }

    if (window_ax_can_resize(window)) {
        window_set_flag(window, WINDOW_RESIZABLE);
    } else {
        window_clear_flag(window, WINDOW_RESIZABLE);
    }

    if (window->role) CFRelease(window->role);
    window->role = window_ax_role(window);

    if (window->subrole) CFRelease(window->subrole);
    window->subrole = window_ax_subrole(window);

    if (window->id == g_window_manager.last_window_id) {
        g_window_manager.last_window_id = g_window_manager.focused_window_id;
    }

    struct view *view = window_manager_find_managed_window(&g_window_manager, window);
    if (view) {
        space_manager_untile_window(view, window);
        window_manager_remove_managed_window(&g_window_manager, window->id);
        window_manager_purify_window(&g_window_manager, window);
    }

    event_signal_push(SIGNAL_WINDOW_MINIMIZED, window);
}

static EVENT_HANDLER(WINDOW_DEMINIMIZED)
{
    struct window *window = context;

    if (!__sync_bool_compare_and_swap(&window->id_ptr, &window->id, &window->id)) {
        debug("%s: %d has been marked invalid by the system, ignoring event..\n", __FUNCTION__, window->id);
        window_manager_remove_lost_focused_event(&g_window_manager, window->id);
        return;
    }

    window_clear_flag(window, WINDOW_MINIMIZE);

    if (window_ax_can_move(window)) {
        window_set_flag(window, WINDOW_MOVABLE);
    } else {
        window_clear_flag(window, WINDOW_MOVABLE);
    }

    if (window_ax_can_resize(window)) {
        window_set_flag(window, WINDOW_RESIZABLE);
    } else {
        window_clear_flag(window, WINDOW_RESIZABLE);
    }

    if (window->role) CFRelease(window->role);
    window->role = window_ax_role(window);

    if (window->subrole) CFRelease(window->subrole);
    window->subrole = window_ax_subrole(window);

    uint64_t sid = space_manager_active_space();
    if (space_manager_is_window_on_space(sid, window)) {
        debug("%s: window %s %d is deminimized on active space\n", __FUNCTION__, window->application->name, window->id);
        if (window_manager_should_manage_window(window) && !window_manager_find_managed_window(&g_window_manager, window)) {
            struct window *last_window = window_manager_find_window(&g_window_manager, g_window_manager.last_window_id);
            uint32_t insertion_point = last_window && last_window->application->pid != window->application->pid ? last_window->id : 0;
            struct view *view = space_manager_tile_window_on_space_with_insertion_point(&g_space_manager, window, sid, insertion_point);
            window_manager_add_managed_window(&g_window_manager, window, view);
        }
    } else {
        debug("%s: window %s %d is deminimized on inactive space\n", __FUNCTION__, window->application->name, window->id);
    }

    if (window_manager_find_lost_focused_event(&g_window_manager, window->id)) {
        event_loop_post(&g_event_loop, WINDOW_FOCUSED, (void *)(intptr_t) window->id, 0);
        window_manager_remove_lost_focused_event(&g_window_manager, window->id);
    }

    event_signal_push(SIGNAL_WINDOW_DEMINIMIZED, window);
}

static EVENT_HANDLER(WINDOW_TITLE_CHANGED)
{
    uint32_t window_id = (uint32_t)(intptr_t) context;
    struct window *window = window_manager_find_window(&g_window_manager, window_id);
    if (!window) return;

    if (!__sync_bool_compare_and_swap(&window->id_ptr, &window->id, &window->id)) {
        debug("%s: %d has been marked invalid by the system, ignoring event..\n", __FUNCTION__, window_id);
        return;
    }

    debug("%s: %s %d\n", __FUNCTION__, window->application->name, window->id);

    if (window->title) CFRelease(window->title);

    window->title = window_title(window);

    event_signal_push(SIGNAL_WINDOW_TITLE_CHANGED, window);
}

static EVENT_HANDLER(SLS_WINDOW_ORDERED)
{
    uint32_t wid = (uint64_t)(intptr_t) context;
    debug("%s: %d\n", __FUNCTION__, wid);
    struct window_node *node = table_find(&g_window_manager.insert_feedback, &wid);
    if (node) SLSOrderWindow(g_connection, node->feedback_window.id, 1, node->window_order[0]);
}

static EVENT_HANDLER(SLS_WINDOW_DESTROYED)
{
    uint32_t wid = (uint64_t)(intptr_t) context;
    debug("%s: %d\n", __FUNCTION__, wid);

    struct window *window = window_manager_find_window(&g_window_manager, wid);
    if (!window) return;

    if (!__sync_bool_compare_and_swap(&window->id_ptr, &window->id, &window->id)) {
        debug("%s: %d has been marked invalid by the system, ignoring event..\n", __FUNCTION__, wid);
        return;
    }

    EVENT_HANDLER_WINDOW_DESTROYED(window, 0);
}
