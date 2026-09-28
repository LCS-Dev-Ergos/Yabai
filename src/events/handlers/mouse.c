// Mouse presses, drags, drops and focus-following moves.
// Runs on the event-loop thread.

static EVENT_HANDLER(MOUSE_DOWN)
{
    space_navigation_snapshot_cancel();
    if (mission_control_is_active())                     goto out;
    if (g_mouse_state.current_action != MOUSE_MODE_NONE) goto out;

    CGPoint point = CGEventGetLocation(context);
    debug("%s: %.2f, %.2f\n", __FUNCTION__, point.x, point.y);

    struct window *window = window_manager_find_window_at_point(&g_window_manager, point);
    if (!window || window_check_flag(window, WINDOW_FULLSCREEN)) goto out;

    g_mouse_state.window = window;
    g_mouse_state.window_frame = g_mouse_state.window->frame;
    g_mouse_state.down_location = point;
    g_mouse_state.direction = 0;

    int64_t button = CGEventGetIntegerValueField(context, kCGMouseEventButtonNumber);
    uint8_t mod = (uint8_t) param1;

    if (button == kCGMouseButtonLeft && g_mouse_state.modifier == mod) {
        g_mouse_state.current_action = g_mouse_state.action1;
    } else if (button == kCGMouseButtonRight && g_mouse_state.modifier == mod) {
        g_mouse_state.current_action = g_mouse_state.action2;
    }

    if (g_mouse_state.current_action == MOUSE_MODE_RESIZE) {
        CGPoint frame_mid = { CGRectGetMidX(g_mouse_state.window_frame), CGRectGetMidY(g_mouse_state.window_frame) };
        if (point.x < frame_mid.x) g_mouse_state.direction |= HANDLE_LEFT;
        if (point.y < frame_mid.y) g_mouse_state.direction |= HANDLE_TOP;
        if (point.x > frame_mid.x) g_mouse_state.direction |= HANDLE_RIGHT;
        if (point.y > frame_mid.y) g_mouse_state.direction |= HANDLE_BOTTOM;
    }

out:
    CFRelease(context);
}

static EVENT_HANDLER(MOUSE_UP)
{
    if (mission_control_is_active()) goto out;
    if (!g_mouse_state.window)       goto res;

    if (!__sync_bool_compare_and_swap(&g_mouse_state.window->id_ptr, &g_mouse_state.window->id, &g_mouse_state.window->id)) {
        debug("%s: %d has been marked invalid by the system, ignoring event..\n", __FUNCTION__, g_mouse_state.window->id);
        goto err;
    }

    if (window_check_flag(g_mouse_state.window, WINDOW_FULLSCREEN)) {
        debug("%s: %d is transitioning into native-fullscreen mode, ignoring event..\n", __FUNCTION__, g_mouse_state.window->id);
        goto err;
    }

    CGPoint point = CGEventGetLocation(context);
    debug("%s: %.2f, %.2f\n", __FUNCTION__, point.x, point.y);

    struct view *src_view = window_manager_find_managed_window(&g_window_manager, g_mouse_state.window);
    if (!src_view) goto err;

    struct mouse_window_info info;
    mouse_window_info_populate(&g_mouse_state, &info);

    if (info.changed_position && !info.changed_size) {
        uint64_t cursor_sid = display_space_id(display_manager_point_display_id(point));
        struct view *dst_view = space_manager_find_view(&g_space_manager, cursor_sid);

        struct window *window = window_manager_find_window_at_point_filtering_window(&g_window_manager, point, g_mouse_state.window->id);
        if (!window) window = window_manager_find_window_at_point(&g_window_manager, point);
        if (window == g_mouse_state.window) window = NULL;

        struct window_node *a_node = view_find_window_node(src_view, g_mouse_state.window->id);
        struct window_node *b_node = window ? view_find_window_node(dst_view, window->id) : NULL;

        if (a_node && b_node && a_node != b_node) {
            if (g_mouse_state.feedback_node) {
                g_mouse_state.feedback_node->insert_dir = 0;
                insert_feedback_destroy(g_mouse_state.feedback_node);
                g_mouse_state.feedback_node = NULL;
            }

            enum mouse_drop_action drop_action = mouse_determine_drop_action(&g_mouse_state, a_node, window, point);
            switch (drop_action) {
            case MOUSE_DROP_ACTION_STACK: {
                mouse_drop_action_stack(&g_window_manager, src_view, g_mouse_state.window, dst_view, window);
            } break;
            case MOUSE_DROP_ACTION_SWAP: {
                mouse_drop_action_swap(&g_window_manager, src_view, a_node, g_mouse_state.window, dst_view, b_node, window);
            } break;
            case MOUSE_DROP_ACTION_WARP_TOP: {
                mouse_drop_action_warp(&g_window_manager, src_view, a_node, g_mouse_state.window, dst_view, b_node, window, SPLIT_X, CHILD_FIRST);
            } break;
            case MOUSE_DROP_ACTION_WARP_RIGHT: {
                mouse_drop_action_warp(&g_window_manager, src_view, a_node, g_mouse_state.window, dst_view, b_node, window, SPLIT_Y, CHILD_SECOND);
            } break;
            case MOUSE_DROP_ACTION_WARP_BOTTOM: {
                mouse_drop_action_warp(&g_window_manager, src_view, a_node, g_mouse_state.window, dst_view, b_node, window, SPLIT_X, CHILD_SECOND);
            } break;
            case MOUSE_DROP_ACTION_WARP_LEFT: {
                mouse_drop_action_warp(&g_window_manager, src_view, a_node, g_mouse_state.window, dst_view, b_node, window, SPLIT_Y, CHILD_FIRST);
            } break;
            case MOUSE_DROP_ACTION_NONE: {
                /* silence compiler warning.. */
            } break;
            }
        } else if (a_node) {
            mouse_drop_no_target(&g_space_manager, &g_window_manager, src_view, dst_view, g_mouse_state.window, a_node);
        }
    } else if (info.changed_position || info.changed_size) {
        mouse_drop_try_adjust_bsp_grid(&g_window_manager, src_view, g_mouse_state.window, &info);
    }

err:
    g_mouse_state.window = NULL;
res:
    g_mouse_state.current_action = MOUSE_MODE_NONE;
out:
    CFRelease(context);
}

static EVENT_HANDLER(MOUSE_DRAGGED)
{
    if (mission_control_is_active()) goto out;
    if (!g_mouse_state.window)       goto out;

    if (!__sync_bool_compare_and_swap(&g_mouse_state.window->id_ptr, &g_mouse_state.window->id, &g_mouse_state.window->id)) {
        debug("%s: %d has been marked invalid by the system, ignoring event..\n", __FUNCTION__, g_mouse_state.window->id);
        g_mouse_state.window = NULL;
        g_mouse_state.current_action = MOUSE_MODE_NONE;
        CFRelease(context);
        return;
    }

    CGPoint point = CGEventGetLocation(context);
    debug("%s: %.2f, %.2f\n", __FUNCTION__, point.x, point.y);

    if (g_mouse_state.current_action == MOUSE_MODE_MOVE) {
        CGPoint new_point = { g_mouse_state.window_frame.origin.x + (point.x - g_mouse_state.down_location.x),
                              g_mouse_state.window_frame.origin.y + (point.y - g_mouse_state.down_location.y) };

        uint32_t did = display_manager_point_display_id(new_point);
        if (did) {
            CGRect bounds = display_bounds_constrained(did, false);
            if (new_point.y < bounds.origin.y) new_point.y = bounds.origin.y;
        }

        if (!scripting_addition_move_window(g_mouse_state.window->id, new_point.x, new_point.y)) {
            window_manager_move_window(g_mouse_state.window, new_point.x, new_point.y);
        }
    } else if (g_mouse_state.current_action == MOUSE_MODE_RESIZE) {
        uint64_t event_time = read_os_timer();
        float dt = ((float) event_time - g_mouse_state.last_moved_time) * (1000.0f / (float)read_os_freq());
        if (dt < 67.67f) goto out;

        int dx = point.x - g_mouse_state.down_location.x;
        int dy = point.y - g_mouse_state.down_location.y;

        window_manager_resize_window_relative_internal(g_mouse_state.window, g_mouse_state.window->frame, g_mouse_state.direction, dx, dy, false);

        g_mouse_state.last_moved_time = event_time;
        g_mouse_state.down_location = point;
    }

    struct view *src_view = window_manager_find_managed_window(&g_window_manager, g_mouse_state.window);
    if (!src_view) goto out;

    struct mouse_window_info info;
    mouse_window_info_populate(&g_mouse_state, &info);

    if (info.changed_position && !info.changed_size) {
        uint64_t cursor_sid = display_space_id(display_manager_point_display_id(point));
        struct view *dst_view = space_manager_find_view(&g_space_manager, cursor_sid);

        struct window *window = window_manager_find_window_at_point_filtering_window(&g_window_manager, point, g_mouse_state.window->id);
        if (!window) window = window_manager_find_window_at_point(&g_window_manager, point);
        if (window == g_mouse_state.window) window = NULL;

        struct window_node *a_node = view_find_window_node(src_view, g_mouse_state.window->id);
        struct window_node *b_node = window ? view_find_window_node(dst_view, window->id) : NULL;

        if (a_node && b_node && a_node != b_node) {
            if (g_mouse_state.feedback_node && g_mouse_state.feedback_node != b_node) {
                g_mouse_state.feedback_node->insert_dir = 0;
                insert_feedback_destroy(g_mouse_state.feedback_node);
            }

            int insert_dir = 0;
            enum mouse_drop_action drop_action = mouse_determine_drop_action(&g_mouse_state, a_node, window, point);
            switch (drop_action) {
            case MOUSE_DROP_ACTION_STACK: {
                insert_dir = STACK;
            } break;
            case MOUSE_DROP_ACTION_SWAP: {
                insert_dir = STACK;
            } break;
            case MOUSE_DROP_ACTION_WARP_TOP: {
                insert_dir = DIR_NORTH;
            } break;
            case MOUSE_DROP_ACTION_WARP_RIGHT: {
                insert_dir = DIR_EAST;
            } break;
            case MOUSE_DROP_ACTION_WARP_BOTTOM: {
                insert_dir = DIR_SOUTH;
            } break;
            case MOUSE_DROP_ACTION_WARP_LEFT: {
                insert_dir = DIR_WEST;
            } break;
            case MOUSE_DROP_ACTION_NONE: {
                /* silence compiler warning.. */
            } break;
            }

            if (b_node->insert_dir != insert_dir) {
                b_node->insert_dir = insert_dir;
                insert_feedback_show(b_node);
                g_mouse_state.feedback_node = b_node;
            }
        } else if (!b_node) {
            if (g_mouse_state.feedback_node) {
                g_mouse_state.feedback_node->insert_dir = 0;
                insert_feedback_destroy(g_mouse_state.feedback_node);
                g_mouse_state.feedback_node = NULL;
            }
        }
    }

out:
    CFRelease(context);
}

static EVENT_HANDLER(MOUSE_MOVED)
{
    if (g_window_manager.ffm_mode == FFM_DISABLED) goto out;
    if (mission_control_is_active())               goto out;
    if (g_mouse_state.ffm_window_id)               goto out;

    if (__atomic_load_n(&__pending_gesture, __ATOMIC_RELAXED)) goto out;
    uint64_t last_gesture_time = __atomic_load_n(&__last_gesture_time, __ATOMIC_RELAXED);
    float dt = ((float) read_os_timer() - last_gesture_time) * (1000.0f / (float)read_os_freq());
    if (dt < 1250.0f) goto out;

    CGPoint point = CGEventGetLocation(context);
    struct window *window = window_manager_find_window_at_point(&g_window_manager, point);

    if (window) {
        if (window->id == g_window_manager.focused_window_id) goto out;
        if (!window_manager_is_window_eligible(window))       goto out;

        if (g_window_manager.ffm_mode == FFM_AUTOFOCUS) {

            //
            // NOTE(asmvik): Look for a window with role AXSheet or AXDrawer
            // and forward focus to it because we are not allowed to focus the main
            // window in these cases.
            //

            CFArrayRef window_list = SLSCopyAssociatedWindows(g_connection, window->id);
            if (window_list) {
                int window_count = CFArrayGetCount(window_list);

                uint32_t child_wid;
                for (int i = 0; i < window_count; ++i) {
                    CFNumberGetValue(CFArrayGetValueAtIndex(window_list, i), kCFNumberSInt32Type, &child_wid);
                    struct window *child = window_manager_find_window(&g_window_manager, child_wid);
                    if (!child) continue;

                    CFTypeRef role = window_role(child);
                    if (!role) continue;

                    bool valid = CFEqual(role, kAXSheetRole) || CFEqual(role, kAXDrawerRole);
                    CFRelease(role);

                    if (valid) {
                        window = child;
                        break;
                    }
                }

                CFRelease(window_list);
            }

            window_manager_focus_window_without_raise(&window->application->psn, window->id);
            g_mouse_state.ffm_window_id = window->id;
        } else if (g_window_manager.ffm_mode == FFM_AUTORAISE) {

            //
            // NOTE(asmvik): If any **floating** window would be fully occluded by
            // autoraising the window below the cursor we do not actually perform the
            // focus change, as it is likely that the user is trying to reach for the
            // smaller window that sits on top of the window we would otherwise raise.
            //

            bool occludes_window = false;

            int window_count;
            uint32_t *window_list = space_window_list(g_space_manager.current_space_id, &window_count, false);

            if (window_list) {
                for (int i = 0; i < window_count; ++i) {
                    uint32_t wid = window_list[i];
                    if (wid == window->id) break;

                    struct window *sub_window = window_manager_find_window(&g_window_manager, wid);
                    if (!sub_window) continue;

                    if (!window_check_flag(sub_window, WINDOW_FLOAT))                     continue;
                    if (window_level(window->id) != window_level(sub_window->id))         continue;
                    if (window_sub_level(window->id) != window_sub_level(sub_window->id)) continue;

                    if (CGRectContainsRect(window->frame, sub_window->frame)) {
                        occludes_window = true;
                        break;
                    }
                }
            }

            if (!occludes_window) {
                window_manager_focus_window_with_raise(&window->application->psn, window->id, window->ref);
                g_mouse_state.ffm_window_id = window->id;
            }
        }
    } else {
        uint32_t cursor_did = display_manager_point_display_id(point);
        if (g_display_manager.current_display_id == cursor_did) goto out;

        CGRect bounds = display_bounds_constrained(cursor_did, false);
        if (!cgrect_contains_point(bounds, point)) goto out;

        uint32_t wid = display_manager_focus_display_with_window_at_point(point);
        if (!wid) display_manager_set_active_display_id(cursor_did);
        g_mouse_state.ffm_window_id = wid;
    }

out:
    CFRelease(context);
}
