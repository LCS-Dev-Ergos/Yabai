// `yabai -m config`: the global settings, or with --space those a Space can
// override. Like every command, it runs on the event loop.

static void handle_domain_config(FILE *rsp, struct token domain, char *message)
{
    TIME_FUNCTION;

    uint64_t sel_sid = 0;
    struct token selector = get_token(&message);
    struct token command  = selector;

    bool found_selector = token_equals(selector, SELECTOR_CONFIG_SPACE);
    if (found_selector) {
        struct selector space_selector = parse_space_selector(rsp, &message, 0, false);
        if (!space_selector.did_parse || !space_selector.sid) return;

        sel_sid = space_selector.sid;
        command = get_token(&message);
    }

    for (; token_is_valid(command); command = get_token(&message)) {
        if (token_equals(command, COMMAND_CONFIG_DEBUG_OUTPUT)) {
            struct token value = get_token(&message);
            if (!token_is_valid(value)) {
                fprintf(rsp, "%s\n", bool_str[__atomic_load_n(&g_verbose, __ATOMIC_RELAXED)]);
            } else if (token_equals(value, ARGUMENT_COMMON_VAL_OFF)) {
                __atomic_store_n(&g_verbose, false, __ATOMIC_RELAXED);
            } else if (token_equals(value, ARGUMENT_COMMON_VAL_ON)) {
                __atomic_store_n(&g_verbose, true, __ATOMIC_RELAXED);
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_CONFIG_MFF)) {
            struct token value = get_token(&message);
            if (!token_is_valid(value)) {
                fprintf(rsp, "%s\n", bool_str[g_window_manager.enable_mff]);
            } else if (token_equals(value, ARGUMENT_COMMON_VAL_OFF)) {
                g_window_manager.enable_mff = false;
            } else if (token_equals(value, ARGUMENT_COMMON_VAL_ON)) {
                g_window_manager.enable_mff = true;
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_CONFIG_FFM)) {
            struct token value = get_token(&message);
            if (!token_is_valid(value)) {
                fprintf(rsp, "%s\n", ffm_mode_str[g_window_manager.ffm_mode]);
            } else if (token_equals(value, ARGUMENT_COMMON_VAL_OFF)) {
                window_manager_set_focus_follows_mouse(&g_window_manager, FFM_DISABLED);
            } else if (token_equals(value, ARGUMENT_CONFIG_FFM_AUTOFOCUS)) {
                window_manager_set_focus_follows_mouse(&g_window_manager, FFM_AUTOFOCUS);
            } else if (token_equals(value, ARGUMENT_CONFIG_FFM_AUTORAISE)) {
                window_manager_set_focus_follows_mouse(&g_window_manager, FFM_AUTORAISE);
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_CONFIG_DISPLAY_ORDER)) {
            struct token value = get_token(&message);
            if (!token_is_valid(value)) {
                fprintf(rsp, "%s\n", display_arrangement_order_str[g_display_manager.order]);
            } else if (token_equals(value, ARGUMENT_CONFIG_DISPLAY_ORDER_DEFAULT)) {
                g_display_manager.order = DISPLAY_ARRANGEMENT_ORDER_DEFAULT;
            } else if (token_equals(value, ARGUMENT_CONFIG_DISPLAY_ORDER_X)) {
                g_display_manager.order = DISPLAY_ARRANGEMENT_ORDER_X;
            } else if (token_equals(value, ARGUMENT_CONFIG_DISPLAY_ORDER_Y)) {
                g_display_manager.order = DISPLAY_ARRANGEMENT_ORDER_Y;
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_CONFIG_WINDOW_ORIGIN)) {
            struct token value = get_token(&message);
            if (!token_is_valid(value)) {
                fprintf(rsp, "%s\n", window_origin_mode_str[g_window_manager.window_origin_mode]);
            } else if (token_equals(value, ARGUMENT_CONFIG_WINDOW_ORIGIN_DEFAULT)) {
                g_window_manager.window_origin_mode = WINDOW_ORIGIN_DEFAULT;
            } else if (token_equals(value, ARGUMENT_CONFIG_WINDOW_ORIGIN_FOCUSED)) {
                g_window_manager.window_origin_mode = WINDOW_ORIGIN_FOCUSED;
            } else if (token_equals(value, ARGUMENT_CONFIG_WINDOW_ORIGIN_CURSOR)) {
                g_window_manager.window_origin_mode = WINDOW_ORIGIN_CURSOR;
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_CONFIG_WINDOW_PLACEMENT)) {
            struct token value = get_token(&message);
            if (!token_is_valid(value)) {
                fprintf(rsp, "%s\n", window_node_child_str[g_space_manager.window_placement]);
            } else if (token_equals(value, ARGUMENT_CONFIG_WINDOW_PLACEMENT_FST)) {
                g_space_manager.window_placement = CHILD_FIRST;
            } else if (token_equals(value, ARGUMENT_CONFIG_WINDOW_PLACEMENT_SND)) {
                g_space_manager.window_placement = CHILD_SECOND;
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_CONFIG_WINDOW_INSERT_POINT)) {
            struct token value = get_token(&message);
            if (!token_is_valid(value)) {
                fprintf(rsp, "%s\n", window_insertion_point_str[g_space_manager.window_insertion_point]);
            } else if (token_equals(value, ARGUMENT_CONFIG_WINDOW_INSERT_FOCUSED)) {
                g_space_manager.window_insertion_point = INSERT_FOCUSED;
            } else if (token_equals(value, ARGUMENT_CONFIG_WINDOW_INSERT_FIRST)) {
                g_space_manager.window_insertion_point = INSERT_FIRST;
            } else if (token_equals(value, ARGUMENT_CONFIG_WINDOW_INSERT_LAST)) {
                g_space_manager.window_insertion_point = INSERT_LAST;
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_CONFIG_WINDOW_ZOOM_PERSIST)) {
            struct token value = get_token(&message);
            if (!token_is_valid(value)) {
                fprintf(rsp, "%s\n", bool_str[g_space_manager.window_zoom_persist]);
            } else if (token_equals(value, ARGUMENT_COMMON_VAL_OFF)) {
                g_space_manager.window_zoom_persist = false;
            } else if (token_equals(value, ARGUMENT_COMMON_VAL_ON)) {
                g_space_manager.window_zoom_persist = true;
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_CONFIG_SKIP_SPACE_ANIMATION)) {
            struct token value = get_token(&message);
            if (!token_is_valid(value)) {
                fprintf(rsp, "%s\n", bool_str[g_space_manager.skip_window_focus_animation]);
            } else if (token_equals(value, ARGUMENT_COMMON_VAL_OFF)) {
                g_space_manager.skip_window_focus_animation = false;
            } else if (token_equals(value, ARGUMENT_COMMON_VAL_ON)) {
                g_space_manager.skip_window_focus_animation = true;
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_CONFIG_NAVIGATION_PACING)) {
            struct token value = get_token(&message);
            if (!token_is_valid(value)) {
                fprintf(rsp, "%s\n", bool_str[space_navigation_schedule_pacing()]);
            } else if (token_equals(value, ARGUMENT_COMMON_VAL_OFF)) {
                space_navigation_schedule_set_pacing(false);
            } else if (token_equals(value, ARGUMENT_COMMON_VAL_ON)) {
                space_navigation_schedule_set_pacing(true);
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_CONFIG_OPACITY)) {
            struct token value = get_token(&message);
            if (!token_is_valid(value)) {
                fprintf(rsp, "%s\n", bool_str[g_window_manager.enable_window_opacity]);
            } else if (token_equals(value, ARGUMENT_COMMON_VAL_OFF)) {
                window_manager_set_window_opacity_enabled(&g_window_manager, false);
            } else if (token_equals(value, ARGUMENT_COMMON_VAL_ON)) {
                window_manager_set_window_opacity_enabled(&g_window_manager, true);
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_CONFIG_OPACITY_DURATION)) {
            struct token_value value = token_to_value(get_token(&message));
            float duration;
            if (value.type == TOKEN_TYPE_INVALID) {
                fprintf(rsp, "%f\n", g_window_manager.window_opacity_duration);
            } else if (token_value_to_finite_float(value, &duration) && duration >= 0.0f) {
                g_window_manager.window_opacity_duration = duration;
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.token.length, value.token.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_CONFIG_ANIMATION_DURATION)) {
            // An animation ends when its elapsed share of the duration reaches
            // 1, which a negative or non-finite duration never lets happen.
            struct token_value value = token_to_value(get_token(&message));
            float duration;
            if (value.type == TOKEN_TYPE_INVALID) {
                fprintf(rsp, "%f\n", g_window_manager.window_animation_duration);
            } else if (token_value_to_finite_float(value, &duration) && duration >= 0.0f) {
                if (duration == 0.0f) {
                    g_window_manager.window_animation_duration = duration;
                } else if (!scripting_addition_is_sip_friendly()) {
                    daemon_fail(rsp, "command '%.*s' for domain '%.*s' requires System Integrity Protection to be partially disabled! ignoring request..\n", command.length, command.text, domain.length, domain.text);
                } else if (CGPreflightScreenCaptureAccess()) {
                    g_window_manager.window_animation_duration = duration;
                } else {
                    daemon_fail(rsp, "command '%.*s' for domain '%.*s' requires Screen Recording permissions! ignoring request..\n", command.length, command.text, domain.length, domain.text);
                    CGRequestScreenCaptureAccess();
                }
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.token.length, value.token.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_CONFIG_ANIMATION_EASING)) {
            struct token value = get_token(&message);
            if (!token_is_valid(value)) {
                fprintf(rsp, "%s\n", animation_easing_type_str[g_window_manager.window_animation_easing]);
            } else {
                bool match = false;
                for (int i = 0; i < EASING_TYPE_COUNT; ++i) {
                    if (token_equals(value, animation_easing_type_str[i])) {
                        g_window_manager.window_animation_easing = i;
                        match = true;
                        break;
                    }
                }
                if (!match) daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_CONFIG_NAVIGATION_FADE_CURVE)) {
            struct token value = get_token(&message);
            if (!token_is_valid(value)) {
                fprintf(rsp, "%s\n", space_snapshot_curve_str[g_window_manager.navigation_fade_curve]);
            } else {
                bool match = false;
                for (int i = 0; i < SPACE_SNAPSHOT_CURVE_COUNT; ++i) {
                    if (token_equals(value, space_snapshot_curve_str[i])) {
                        g_window_manager.navigation_fade_curve = i;
                        match = true;
                        break;
                    }
                }
                if (!match) daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_CONFIG_NAVIGATION_VEIL_BLUR)) {
            struct token_value value = token_to_value(get_token(&message));
            if (value.type == TOKEN_TYPE_INVALID) {
                fprintf(rsp, "%d\n", g_window_manager.navigation_veil_blur);
            } else if (value.type == TOKEN_TYPE_INT && in_range_ii(value.int_value, 0, 100)) {
                g_window_manager.navigation_veil_blur = value.int_value;
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.token.length, value.token.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_CONFIG_SHADOW)) {
            struct token value = get_token(&message);
            if (!token_is_valid(value)) {
                fprintf(rsp, "%s\n", purify_mode_str[g_window_manager.purify_mode]);
            } else if (token_equals(value, ARGUMENT_COMMON_VAL_OFF)) {
                window_manager_set_purify_mode(&g_window_manager, PURIFY_ALWAYS);
            } else if (token_equals(value, ARGUMENT_CONFIG_SHADOW_FLT)) {
                window_manager_set_purify_mode(&g_window_manager, PURIFY_MANAGED);
            } else if (token_equals(value, ARGUMENT_COMMON_VAL_ON)) {
                window_manager_set_purify_mode(&g_window_manager, PURIFY_DISABLED);
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_CONFIG_MENUBAR_OPACITY)) {
            struct token_value value = token_to_value(get_token(&message));
            float opacity;
            if (value.type == TOKEN_TYPE_INVALID) {
                fprintf(rsp, "%.4f\n", g_window_manager.menubar_opacity);
            } else if (token_value_to_finite_float(value, &opacity) && in_range_ii(opacity, 0.0f, 1.0f)) {
                window_manager_set_menubar_opacity(&g_window_manager, opacity);
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.token.length, value.token.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_CONFIG_ACTIVE_WINDOW_OPACITY)) {
            struct token_value value = token_to_value(get_token(&message));
            float opacity;
            if (value.type == TOKEN_TYPE_INVALID) {
                fprintf(rsp, "%.4f\n", g_window_manager.active_window_opacity);
            } else if (token_value_to_finite_float(value, &opacity) && in_range_ei(opacity, 0.0f, 1.0f)) {
                window_manager_set_active_window_opacity(&g_window_manager, opacity);
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.token.length, value.token.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_CONFIG_NORMAL_WINDOW_OPACITY)) {
            struct token_value value = token_to_value(get_token(&message));
            float opacity;
            if (value.type == TOKEN_TYPE_INVALID) {
                fprintf(rsp, "%.4f\n", g_window_manager.normal_window_opacity);
            } else if (token_value_to_finite_float(value, &opacity) && in_range_ei(opacity, 0.0f, 1.0f)) {
                window_manager_set_normal_window_opacity(&g_window_manager, opacity);
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.token.length, value.token.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_CONFIG_INSERT_FEEDBACK_COLOR)) {
            struct token_value value = token_to_value(get_token(&message));
            if (value.type == TOKEN_TYPE_INVALID) {
                fprintf(rsp, "0x%x\n", g_window_manager.insert_feedback_color.p);
            } else if (value.type == TOKEN_TYPE_U32 && value.u32_value) {
                g_window_manager.insert_feedback_color = rgba_color_from_hex(value.u32_value);
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.token.length, value.token.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_CONFIG_TOP_PADDING)) {
            struct token_value value = token_to_value(get_token(&message));
            if (sel_sid) {
                struct view *view = space_manager_find_view(&g_space_manager, sel_sid);
                if (value.type == TOKEN_TYPE_INVALID) {
                    fprintf(rsp, "%d\n", view->top_padding);
                } else if (value.type == TOKEN_TYPE_INT) {
                    view_set_flag(view, VIEW_TOP_PADDING);
                    view->top_padding = value.int_value;
                    view_update(view);
                    view_flush(view);
                } else {
                    daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.token.length, value.token.text, command.length, command.text, domain.length, domain.text);
                }
            } else {
                if (value.type == TOKEN_TYPE_INVALID) {
                    fprintf(rsp, "%d\n", g_space_manager.top_padding);
                } else if (value.type == TOKEN_TYPE_INT) {
                    space_manager_set_top_padding_for_all_spaces(&g_space_manager, value.int_value);
                } else {
                    daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.token.length, value.token.text, command.length, command.text, domain.length, domain.text);
                }
            }
        } else if (token_equals(command, COMMAND_CONFIG_BOTTOM_PADDING)) {
            struct token_value value = token_to_value(get_token(&message));
            if (sel_sid) {
                struct view *view = space_manager_find_view(&g_space_manager, sel_sid);
                if (value.type == TOKEN_TYPE_INVALID) {
                    fprintf(rsp, "%d\n", view->bottom_padding);
                } else if (value.type == TOKEN_TYPE_INT) {
                    view_set_flag(view, VIEW_BOTTOM_PADDING);
                    view->bottom_padding = value.int_value;
                    view_update(view);
                    view_flush(view);
                } else {
                    daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.token.length, value.token.text, command.length, command.text, domain.length, domain.text);
                }
            } else {
                if (value.type == TOKEN_TYPE_INVALID) {
                    fprintf(rsp, "%d\n", g_space_manager.bottom_padding);
                } else if (value.type == TOKEN_TYPE_INT) {
                    space_manager_set_bottom_padding_for_all_spaces(&g_space_manager, value.int_value);
                } else {
                    daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.token.length, value.token.text, command.length, command.text, domain.length, domain.text);
                }
            }
        } else if (token_equals(command, COMMAND_CONFIG_LEFT_PADDING)) {
            struct token_value value = token_to_value(get_token(&message));
            if (sel_sid) {
                struct view *view = space_manager_find_view(&g_space_manager, sel_sid);
                if (value.type == TOKEN_TYPE_INVALID) {
                    fprintf(rsp, "%d\n", view->left_padding);
                } else if (value.type == TOKEN_TYPE_INT) {
                    view_set_flag(view, VIEW_LEFT_PADDING);
                    view->left_padding = value.int_value;
                    view_update(view);
                    view_flush(view);
                } else {
                    daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.token.length, value.token.text, command.length, command.text, domain.length, domain.text);
                }
            } else {
                if (value.type == TOKEN_TYPE_INVALID) {
                    fprintf(rsp, "%d\n", g_space_manager.left_padding);
                } else if (value.type == TOKEN_TYPE_INT) {
                    space_manager_set_left_padding_for_all_spaces(&g_space_manager, value.int_value);
                } else {
                    daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.token.length, value.token.text, command.length, command.text, domain.length, domain.text);
                }
            }
        } else if (token_equals(command, COMMAND_CONFIG_RIGHT_PADDING)) {
            struct token_value value = token_to_value(get_token(&message));
            if (sel_sid) {
                struct view *view = space_manager_find_view(&g_space_manager, sel_sid);
                if (value.type == TOKEN_TYPE_INVALID) {
                    fprintf(rsp, "%d\n", view->right_padding);
                } else if (value.type == TOKEN_TYPE_INT) {
                    view_set_flag(view, VIEW_RIGHT_PADDING);
                    view->right_padding = value.int_value;
                    view_update(view);
                    view_flush(view);
                } else {
                    daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.token.length, value.token.text, command.length, command.text, domain.length, domain.text);
                }
            } else {
                if (value.type == TOKEN_TYPE_INVALID) {
                    fprintf(rsp, "%d\n", g_space_manager.right_padding);
                } else if (value.type == TOKEN_TYPE_INT) {
                    space_manager_set_right_padding_for_all_spaces(&g_space_manager, value.int_value);
                } else {
                    daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.token.length, value.token.text, command.length, command.text, domain.length, domain.text);
                }
            }
        } else if (token_equals(command, COMMAND_CONFIG_WINDOW_GAP)) {
            struct token_value value = token_to_value(get_token(&message));
            if (sel_sid) {
                struct view *view = space_manager_find_view(&g_space_manager, sel_sid);
                if (value.type == TOKEN_TYPE_INVALID) {
                    fprintf(rsp, "%d\n", view->window_gap);
                } else if (value.type == TOKEN_TYPE_INT) {
                    view_set_flag(view, VIEW_WINDOW_GAP);
                    view->window_gap = value.int_value;
                    view_update(view);
                    view_flush(view);
                } else {
                    daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.token.length, value.token.text, command.length, command.text, domain.length, domain.text);
                }
            } else {
                if (value.type == TOKEN_TYPE_INVALID) {
                    fprintf(rsp, "%d\n", g_space_manager.window_gap);
                } else if (value.type == TOKEN_TYPE_INT) {
                    space_manager_set_window_gap_for_all_spaces(&g_space_manager, value.int_value);
                } else {
                    daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.token.length, value.token.text, command.length, command.text, domain.length, domain.text);
                }
            }
        } else if (token_equals(command, COMMAND_CONFIG_LAYOUT)) {
            struct token value = get_token(&message);
            if (sel_sid) {
                struct view *view = space_manager_find_view(&g_space_manager, sel_sid);
                if (!token_is_valid(value)) {
                    fprintf(rsp, "%s\n", view_type_str[view->layout]);
                } else if (token_equals(value, ARGUMENT_CONFIG_LAYOUT_BSP)) {
                    if (space_is_user(sel_sid)) {
                        view_set_flag(view, VIEW_LAYOUT);
                        view->layout = VIEW_BSP;
                        view_clear(view);
                        window_manager_validate_and_check_for_windows_on_space(&g_space_manager, &g_window_manager, sel_sid);
                    } else {
                        daemon_fail(rsp, "cannot set layout for a macOS fullscreen space!\n");
                    }
                } else if (token_equals(value, ARGUMENT_CONFIG_LAYOUT_STACK)) {
                    if (space_is_user(sel_sid)) {
                        view_set_flag(view, VIEW_LAYOUT);
                        view->layout = VIEW_STACK;
                        view_clear(view);
                        window_manager_validate_and_check_for_windows_on_space(&g_space_manager, &g_window_manager, sel_sid);
                    } else {
                        daemon_fail(rsp, "cannot set layout for a macOS fullscreen space!\n");
                    }
                } else if (token_equals(value, ARGUMENT_CONFIG_LAYOUT_FLOAT)) {
                    if (space_is_user(sel_sid)) {
                        view_set_flag(view, VIEW_LAYOUT);
                        view->layout = VIEW_FLOAT;
                        view_clear(view);
                    } else {
                        daemon_fail(rsp, "cannot set layout for a macOS fullscreen space!\n");
                    }
                } else {
                    daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
                }
            } else {
                if (!token_is_valid(value)) {
                    fprintf(rsp, "%s\n", view_type_str[g_space_manager.layout]);
                } else if (token_equals(value, ARGUMENT_CONFIG_LAYOUT_BSP)) {
                    space_manager_set_layout_for_all_spaces(&g_space_manager, VIEW_BSP);
                } else if (token_equals(value, ARGUMENT_CONFIG_LAYOUT_STACK)) {
                    space_manager_set_layout_for_all_spaces(&g_space_manager, VIEW_STACK);
                } else if (token_equals(value, ARGUMENT_CONFIG_LAYOUT_FLOAT)) {
                    space_manager_set_layout_for_all_spaces(&g_space_manager, VIEW_FLOAT);
                } else {
                    daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
                }
            }
        } else if (token_equals(command, COMMAND_CONFIG_SPLIT_RATIO)) {
            struct token_value value = token_to_value(get_token(&message));
            float ratio;
            if (value.type == TOKEN_TYPE_INVALID) {
                fprintf(rsp, "%.4f\n", g_space_manager.split_ratio);
            } else if (token_value_to_finite_float(value, &ratio) && in_range_ii(ratio, 0.1f, 0.9f)) {
                g_space_manager.split_ratio = ratio;
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.token.length, value.token.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_CONFIG_SPLIT_TYPE)) {
            struct token value = get_token(&message);
            if (sel_sid) {
                struct view *view = space_manager_find_view(&g_space_manager, sel_sid);
                if (!token_is_valid(value)) {
                    fprintf(rsp, "%s\n", window_node_split_str[view->split_type]);
                } else if (token_equals(value, ARGUMENT_CONFIG_SPLIT_TYPE_Y)) {
                    view_set_flag(view, VIEW_SPLIT_TYPE);
                    view->split_type = SPLIT_Y;
                } else if (token_equals(value, ARGUMENT_CONFIG_SPLIT_TYPE_X)) {
                    view_set_flag(view, VIEW_SPLIT_TYPE);
                    view->split_type = SPLIT_X;
                } else if (token_equals(value, ARGUMENT_CONFIG_SPLIT_TYPE_AUTO)) {
                    view_set_flag(view, VIEW_SPLIT_TYPE);
                    view->split_type = SPLIT_AUTO;
                } else {
                    daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
                }
            } else {
                if (!token_is_valid(value)) {
                    fprintf(rsp, "%s\n", window_node_split_str[g_space_manager.split_type]);
                } else if (token_equals(value, ARGUMENT_CONFIG_SPLIT_TYPE_Y)) {
                    space_manager_set_split_type_for_all_spaces(&g_space_manager, SPLIT_Y);
                } else if (token_equals(value, ARGUMENT_CONFIG_SPLIT_TYPE_X)) {
                    space_manager_set_split_type_for_all_spaces(&g_space_manager, SPLIT_X);
                } else if (token_equals(value, ARGUMENT_CONFIG_SPLIT_TYPE_AUTO)) {
                    space_manager_set_split_type_for_all_spaces(&g_space_manager, SPLIT_AUTO);
                } else {
                    daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
                }
            }
        } else if (token_equals(command, COMMAND_CONFIG_AUTO_BALANCE)) {
            struct token value = get_token(&message);
            if (sel_sid) {
                struct view *view = space_manager_find_view(&g_space_manager, sel_sid);
                if (!token_is_valid(value)) {
                    fprintf(rsp, "%s\n", auto_balance_str[view->auto_balance]);
                } else if (token_equals(value, ARGUMENT_COMMON_VAL_OFF)) {
                    view_set_flag(view, VIEW_AUTO_BALANCE);
                    view->auto_balance = SPLIT_NONE;
                } else if (token_equals(value, ARGUMENT_COMMON_VAL_ON)) {
                    view_set_flag(view, VIEW_AUTO_BALANCE);
                    view->auto_balance = SPLIT_X | SPLIT_Y;
                } else if (token_equals(value, ARGUMENT_COMMON_VAL_AXIS_X)) {
                    view_set_flag(view, VIEW_AUTO_BALANCE);
                    view->auto_balance = SPLIT_X;
                } else if (token_equals(value, ARGUMENT_COMMON_VAL_AXIS_Y)) {
                    view_set_flag(view, VIEW_AUTO_BALANCE);
                    view->auto_balance = SPLIT_Y;
                } else {
                    daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
                }
            } else {
                if (!token_is_valid(value)) {
                    fprintf(rsp, "%s\n", auto_balance_str[g_space_manager.auto_balance]);
                } else if (token_equals(value, ARGUMENT_COMMON_VAL_OFF)) {
                    space_manager_set_auto_balance_for_all_spaces(&g_space_manager, SPLIT_NONE);
                } else if (token_equals(value, ARGUMENT_COMMON_VAL_ON)) {
                    space_manager_set_auto_balance_for_all_spaces(&g_space_manager, SPLIT_X | SPLIT_Y);
                } else if (token_equals(value, ARGUMENT_COMMON_VAL_AXIS_X)) {
                    space_manager_set_auto_balance_for_all_spaces(&g_space_manager, SPLIT_X);
                } else if (token_equals(value, ARGUMENT_COMMON_VAL_AXIS_Y)) {
                    space_manager_set_auto_balance_for_all_spaces(&g_space_manager, SPLIT_Y);
                } else {
                    daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
                }
            }
        } else if (token_equals(command, COMMAND_CONFIG_MOUSE_MOD)) {
            struct token value = get_token(&message);
            if (!token_is_valid(value)) {
                fprintf(rsp, "%s\n", mouse_mod_str[mouse_modifier_load(&g_mouse_state)]);
            } else if (token_equals(value, ARGUMENT_CONFIG_MOUSE_MOD_ALT)) {
                mouse_modifier_store(&g_mouse_state, MOUSE_MOD_ALT);
            } else if (token_equals(value, ARGUMENT_CONFIG_MOUSE_MOD_SHIFT)) {
                mouse_modifier_store(&g_mouse_state, MOUSE_MOD_SHIFT);
            } else if (token_equals(value, ARGUMENT_CONFIG_MOUSE_MOD_CMD)) {
                mouse_modifier_store(&g_mouse_state, MOUSE_MOD_CMD);
            } else if (token_equals(value, ARGUMENT_CONFIG_MOUSE_MOD_CTRL)) {
                mouse_modifier_store(&g_mouse_state, MOUSE_MOD_CTRL);
            } else if (token_equals(value, ARGUMENT_CONFIG_MOUSE_MOD_FN)) {
                mouse_modifier_store(&g_mouse_state, MOUSE_MOD_FN);
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_CONFIG_MOUSE_ACTION1)) {
            struct token value = get_token(&message);
            if (!token_is_valid(value)) {
                fprintf(rsp, "%s\n", mouse_mode_str[g_mouse_state.action1]);
            } else if (token_equals(value, ARGUMENT_CONFIG_MOUSE_ACTION_MOVE)) {
                g_mouse_state.action1 = MOUSE_MODE_MOVE;
            } else if (token_equals(value, ARGUMENT_CONFIG_MOUSE_ACTION_RESIZE)) {
                g_mouse_state.action1 = MOUSE_MODE_RESIZE;
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_CONFIG_MOUSE_ACTION2)) {
            struct token value = get_token(&message);
            if (!token_is_valid(value)) {
                fprintf(rsp, "%s\n", mouse_mode_str[g_mouse_state.action2]);
            } else if (token_equals(value, ARGUMENT_CONFIG_MOUSE_ACTION_MOVE)) {
                g_mouse_state.action2 = MOUSE_MODE_MOVE;
            } else if (token_equals(value, ARGUMENT_CONFIG_MOUSE_ACTION_RESIZE)) {
                g_mouse_state.action2 = MOUSE_MODE_RESIZE;
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_CONFIG_MOUSE_DROP_ACTION)) {
            struct token value = get_token(&message);
            if (!token_is_valid(value)) {
                fprintf(rsp, "%s\n", mouse_mode_str[g_mouse_state.drop_action]);
            } else if (token_equals(value, ARGUMENT_CONFIG_MOUSE_ACTION_SWAP)) {
                g_mouse_state.drop_action = MOUSE_MODE_SWAP;
            } else if (token_equals(value, ARGUMENT_CONFIG_MOUSE_ACTION_STACK)) {
                g_mouse_state.drop_action = MOUSE_MODE_STACK;
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_CONFIG_EXTERNAL_BAR)) {
            int t, b;
            char mode[6];
            struct token value = get_token(&message);
            if ((sscanf(value.text, ARGUMENT_CONFIG_EXTERNAL_BAR, mode, &t, &b) == 3)) {
                if (string_equals(mode, ARGUMENT_CONFIG_EXTERNAL_BAR_MAIN)) {
                    g_display_manager.mode = EXTERNAL_BAR_MAIN;
                    g_display_manager.top_padding = t;
                    g_display_manager.bottom_padding = b;
                    space_manager_mark_spaces_invalid(&g_space_manager);
                } else if (string_equals(mode, ARGUMENT_CONFIG_EXTERNAL_BAR_ALL)) {
                    g_display_manager.mode = EXTERNAL_BAR_ALL;
                    g_display_manager.top_padding = t;
                    g_display_manager.bottom_padding = b;
                    space_manager_mark_spaces_invalid(&g_space_manager);
                } else if (string_equals(mode, ARGUMENT_COMMON_VAL_OFF)) {
                    g_display_manager.mode = EXTERNAL_BAR_OFF;
                    g_display_manager.top_padding = t;
                    g_display_manager.bottom_padding = b;
                    space_manager_mark_spaces_invalid(&g_space_manager);
                } else {
                    daemon_fail(rsp, "unknown mode '%s' specified in value '%.*s' given to command '%.*s' for domain '%.*s'\n", mode, value.length, value.text, command.length, command.text, domain.length, domain.text);
                }
            } else {
                fprintf(rsp, "%s:%d:%d\n", external_bar_mode_str[g_display_manager.mode], g_display_manager.top_padding, g_display_manager.bottom_padding);
            }
        } else {
            daemon_fail(rsp, "unknown command '%.*s' for domain '%.*s'\n", command.length, command.text, domain.length, domain.text);
        }
    }
}
