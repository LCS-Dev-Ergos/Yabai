// `yabai -m window`: focus, placement in the tree and on Spaces and displays,
// size, stacking, layers, opacity and state. Event loop.

static void handle_domain_window(FILE *rsp, struct token domain, char *message)
{
    TIME_FUNCTION;

    struct token command;
    struct window *acting_window = window_manager_focused_window(&g_window_manager);
    struct selector selector = parse_window_selector(NULL, &message, acting_window, true);

    if (selector.did_parse) {
        acting_window = selector.window;
        command = get_token(&message);
    } else {
        command = selector.token;
    }

    for (; token_is_valid(command); command = get_token(&message)) {
        if (!acting_window &&
            !token_equals(command, COMMAND_WINDOW_FOCUS) &&
            !token_equals(command, COMMAND_WINDOW_CLOSE) &&
            !token_equals(command, COMMAND_WINDOW_MINIMIZE) &&
            !token_equals(command, COMMAND_WINDOW_DEMINIMIZE) &&
            !token_equals(command, COMMAND_WINDOW_TOGGLE)) {
            daemon_fail(rsp, "could not locate the window to act on!\n");
            return;
        }

        if (token_equals(command, COMMAND_WINDOW_FOCUS)) {
            struct selector selector = parse_window_selector(rsp, &message, acting_window, true);

            if (token_is_valid(selector.token)) {
                if (selector.did_parse && selector.window) {
                    acting_window = selector.window;
                } else {
                    return;
                }
            }

            if (acting_window) {
                window_manager_focus_window_with_raise(&acting_window->application->psn, acting_window->id, acting_window->ref);
            } else {
                daemon_fail(rsp, "could not locate the window to act on!\n");
            }
        } else if (token_equals(command, COMMAND_WINDOW_CLOSE)) {
            struct selector selector = parse_window_selector(rsp, &message, acting_window, true);

            if (token_is_valid(selector.token)) {
                if (selector.did_parse && selector.window) {
                    acting_window = selector.window;
                } else {
                    return;
                }
            }

            if (acting_window) {
                if (!window_manager_close_window(acting_window)) {
                    daemon_fail(rsp, "could not close window with id '%d'.\n", acting_window->id);
                }
            } else {
                daemon_fail(rsp, "could not locate the window to act on!\n");
            }
        } else if (token_equals(command, COMMAND_WINDOW_MINIMIZE)) {
            struct selector selector = parse_window_selector(rsp, &message, acting_window, true);

            if (token_is_valid(selector.token)) {
                if (selector.did_parse && selector.window) {
                    acting_window = selector.window;
                } else {
                    return;
                }
            }

            if (acting_window) {
                enum window_op_error result = window_manager_minimize_window(acting_window);
                if (result == WINDOW_OP_ERROR_CANT_MINIMIZE) {
                    daemon_fail(rsp, "window with id '%d' does not support the minimize operation.\n", acting_window->id);
                } else if (result == WINDOW_OP_ERROR_ALREADY_MINIMIZED) {
                    daemon_fail(rsp, "window with id '%d' is already minimized.\n", acting_window->id);
                } else if (result == WINDOW_OP_ERROR_MINIMIZE_FAILED) {
                    daemon_fail(rsp, "could not minimize window with id '%d'.\n", acting_window->id);
                }
            } else {
                daemon_fail(rsp, "could not locate the window to act on!\n");
            }
        } else if (token_equals(command, COMMAND_WINDOW_DEMINIMIZE)) {
            struct selector selector = parse_window_selector(rsp, &message, acting_window, false);
            if (selector.did_parse && selector.window) {
                enum window_op_error result = window_manager_deminimize_window(selector.window);
                if (result == WINDOW_OP_ERROR_NOT_MINIMIZED) {
                    daemon_fail(rsp, "window with id '%d' is not minimized.\n", selector.window->id);
                } else if (result == WINDOW_OP_ERROR_DEMINIMIZE_FAILED) {
                    daemon_fail(rsp, "could not deminimize window with id '%d'.\n", selector.window->id);
                }
            }
        } else if (token_equals(command, COMMAND_WINDOW_DISPLAY)) {
            struct selector selector = parse_display_selector(rsp, &message, display_manager_active_display_id(), false);
            if (selector.did_parse && selector.did) {
                uint64_t sid = display_space_id(selector.did);
                if (space_is_fullscreen(sid)) {
                    daemon_fail(rsp, "can not move window to a macOS fullscreen space!\n");
                } else {
                    window_manager_send_window_to_space(&g_space_manager, &g_window_manager, acting_window, sid, false);
                }
            }
        } else if (token_equals(command, COMMAND_WINDOW_SPACE)) {
            struct selector selector = parse_space_selector(rsp, &message, space_manager_active_space(), false);
            if (selector.did_parse && selector.sid) {
                if (space_is_fullscreen(selector.sid)) {
                    daemon_fail(rsp, "can not move window to a macOS fullscreen space!\n");
                } else {
                    window_manager_send_window_to_space(&g_space_manager, &g_window_manager, acting_window, selector.sid, false);
                }
            }
        } else if (token_equals(command, COMMAND_WINDOW_SWAP)) {
            struct selector selector = parse_window_selector(rsp, &message, acting_window, false);
            if (selector.did_parse && selector.window) {
                enum window_op_error result = window_manager_swap_window(&g_space_manager, &g_window_manager, acting_window, selector.window);
                if (result == WINDOW_OP_ERROR_INVALID_SRC_VIEW) {
                    daemon_fail(rsp, "the acting window is not within a bsp space.\n");
                } else if (result == WINDOW_OP_ERROR_INVALID_DST_VIEW) {
                    daemon_fail(rsp, "the selected window is not within a bsp space.\n");
                } else if (result == WINDOW_OP_ERROR_INVALID_SRC_NODE) {
                    daemon_fail(rsp, "the acting window is not managed.\n");
                } else if (result == WINDOW_OP_ERROR_INVALID_DST_NODE) {
                    daemon_fail(rsp, "the selected window is not managed.\n");
                } else if (result == WINDOW_OP_ERROR_SAME_STACK) {
                    daemon_fail(rsp, "cannot swap a window with a window in the same stack.\n");
                } else if (result == WINDOW_OP_ERROR_SAME_WINDOW) {
                    daemon_fail(rsp, "cannot swap a window with itself.\n");
                }
            }
        } else if (token_equals(command, COMMAND_WINDOW_WARP)) {
            struct selector selector = parse_window_selector(rsp, &message, acting_window, false);
            if (selector.did_parse && selector.window) {
                enum window_op_error result = window_manager_warp_window(&g_space_manager, &g_window_manager, acting_window, selector.window);
                if (result == WINDOW_OP_ERROR_INVALID_SRC_VIEW) {
                    daemon_fail(rsp, "the acting window is not within a bsp space.\n");
                } else if (result == WINDOW_OP_ERROR_INVALID_DST_VIEW) {
                    daemon_fail(rsp, "the selected window is not within a bsp space.\n");
                } else if (result == WINDOW_OP_ERROR_INVALID_SRC_NODE) {
                    daemon_fail(rsp, "the acting window is not managed.\n");
                } else if (result == WINDOW_OP_ERROR_INVALID_DST_NODE) {
                    daemon_fail(rsp, "the selected window is not managed.\n");
                } else if (result == WINDOW_OP_ERROR_SAME_STACK) {
                    daemon_fail(rsp, "cannot warp a window with a window in the same stack.\n");
                } else if (result == WINDOW_OP_ERROR_SAME_WINDOW) {
                    daemon_fail(rsp, "cannot warp a window onto itself.\n");
                }
            }
        } else if (token_equals(command, COMMAND_WINDOW_STACK)) {
            struct selector selector = parse_window_selector(rsp, &message, acting_window, false);
            if (selector.did_parse && selector.window) {
                enum window_op_error result = window_manager_stack_window(&g_space_manager, &g_window_manager, acting_window, selector.window);
                if (result == WINDOW_OP_ERROR_INVALID_SRC_NODE) {
                    daemon_fail(rsp, "the acting window is not managed.\n");
                } else if (result == WINDOW_OP_ERROR_MAX_STACK) {
                    daemon_fail(rsp, "cannot stack window, max capacity of %d reached.\n", NODE_MAX_WINDOW_COUNT);
                } else if (result == WINDOW_OP_ERROR_SAME_WINDOW) {
                    daemon_fail(rsp, "cannot stack a window onto itself.\n");
                }
            }
        } else if (token_equals(command, COMMAND_WINDOW_INSERT)) {
            struct selector selector = parse_insert_selector(rsp, &message);
            if (selector.did_parse && selector.dir) {
                enum window_op_error result = window_manager_set_window_insertion(&g_space_manager, acting_window, selector.dir);
                if (result == WINDOW_OP_ERROR_INVALID_SRC_VIEW) {
                    daemon_fail(rsp, "the acting window is not within a bsp space.\n");
                } else if (result == WINDOW_OP_ERROR_INVALID_SRC_NODE) {
                    daemon_fail(rsp, "the acting window is not managed.\n");
                }
            }
        } else if (token_equals(command, COMMAND_WINDOW_GRID)) {
            unsigned grid[6];
            struct token value = get_token(&message);
            if (parse_grid(value.text, grid)) {
                enum window_op_error result = window_manager_apply_grid(&g_space_manager, &g_window_manager, acting_window, grid[0], grid[1], grid[2], grid[3], grid[4], grid[5]);
                if (result == WINDOW_OP_ERROR_INVALID_SRC_VIEW) {
                    daemon_fail(rsp, "cannot apply grid layout to a managed window.\n");
                }
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_WINDOW_MOVE)) {
            float x, y;
            char type[MAXLEN];
            struct token value = get_token(&message);
            if ((sscanf(value.text, ARGUMENT_WINDOW_MOVE, type, &x, &y) == 3) && isfinite(x) && isfinite(y)) {
                enum window_op_error result = window_manager_move_window_relative(&g_window_manager, acting_window, parse_value_type(type), x, y);
                if (result == WINDOW_OP_ERROR_INVALID_SRC_VIEW) {
                    daemon_fail(rsp, "cannot move a managed window.\n");
                }
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_WINDOW_RESIZE)) {
            float w, h;
            char handle[MAXLEN];
            struct token value = get_token(&message);
            if ((sscanf(value.text, ARGUMENT_WINDOW_RESIZE, handle, &w, &h) == 3) && isfinite(w) && isfinite(h)) {
                enum window_op_error result = window_manager_resize_window_relative(&g_window_manager, acting_window, parse_resize_handle(handle), w, h, true);
                if (result == WINDOW_OP_ERROR_INVALID_SRC_NODE) {
                    daemon_fail(rsp, "cannot locate bsp node for the managed window.\n");
                } else if (result == WINDOW_OP_ERROR_INVALID_DST_NODE) {
                    daemon_fail(rsp, "cannot locate a bsp node fence.\n");
                } else if (result == WINDOW_OP_ERROR_INVALID_OPERATION) {
                    daemon_fail(rsp, "cannot use absolute resizing on a managed window.\n");
                }
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_WINDOW_RATIO)) {
            // A NaN ratio passes the clamp and would stay in the tree.
            float r;
            char type[MAXLEN];
            struct token value = get_token(&message);
            if ((sscanf(value.text, ARGUMENT_WINDOW_RATIO, type, &r) == 2) && isfinite(r)) {
                enum window_op_error result = window_manager_adjust_window_ratio(&g_window_manager, acting_window, parse_value_type(type), r);
                if (result == WINDOW_OP_ERROR_INVALID_SRC_VIEW) {
                    daemon_fail(rsp, "cannot adjust ratio of a non-managed window.\n");
                } else if (result == WINDOW_OP_ERROR_INVALID_SRC_NODE) {
                    daemon_fail(rsp, "cannot adjust ratio of a root node.\n");
                }
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_WINDOW_TOGGLE)) {
            struct token value = get_token(&message);
            if (token_equals(value, ARGUMENT_WINDOW_TOGGLE_FLOAT)) {
                if (acting_window) {
                    window_manager_make_window_floating(&g_space_manager, &g_window_manager, acting_window, !window_check_flag(acting_window, WINDOW_FLOAT), false);
                } else {
                    daemon_fail(rsp, "could not locate the window to act on!\n");
                }
            } else if (token_equals(value, ARGUMENT_WINDOW_TOGGLE_STICKY)) {
                if (acting_window) {
                    window_manager_make_window_sticky(&g_space_manager, &g_window_manager, acting_window, !window_check_flag(acting_window, WINDOW_STICKY));
                } else {
                    daemon_fail(rsp, "could not locate the window to act on!\n");
                }
            } else if (token_equals(value, ARGUMENT_WINDOW_TOGGLE_SHADOW)) {
                if (acting_window) {
                    window_manager_toggle_window_shadow(acting_window);
                } else {
                    daemon_fail(rsp, "could not locate the window to act on!\n");
                }
            } else if (token_equals(value, ARGUMENT_WINDOW_TOGGLE_SPLIT)) {
                if (acting_window) {
                    space_manager_toggle_window_split(&g_space_manager, acting_window);
                } else {
                    daemon_fail(rsp, "could not locate the window to act on!\n");
                }
            } else if (token_equals(value, ARGUMENT_WINDOW_TOGGLE_PARENT)) {
                if (acting_window) {
                    window_manager_toggle_window_zoom_parent(&g_window_manager, acting_window);
                } else {
                    daemon_fail(rsp, "could not locate the window to act on!\n");
                }
            } else if (token_equals(value, ARGUMENT_WINDOW_TOGGLE_FULLSC)) {
                if (acting_window) {
                    window_manager_toggle_window_zoom_fullscreen(&g_window_manager, acting_window);
                } else {
                    daemon_fail(rsp, "could not locate the window to act on!\n");
                }
            } else if (token_equals(value, ARGUMENT_WINDOW_TOGGLE_WINDOWED)) {
                if (acting_window) {
                    window_manager_toggle_window_windowed_fullscreen(acting_window);
                } else {
                    daemon_fail(rsp, "could not locate the window to act on!\n");
                }
            } else if (token_equals(value, ARGUMENT_WINDOW_TOGGLE_NATIVE)) {
                if (acting_window) {
                    window_manager_toggle_window_native_fullscreen(acting_window);
                } else {
                    daemon_fail(rsp, "could not locate the window to act on!\n");
                }
            } else if (token_equals(value, ARGUMENT_WINDOW_TOGGLE_EXPOSE)) {
                if (acting_window) {
                    window_manager_toggle_window_expose(acting_window);
                } else {
                    daemon_fail(rsp, "could not locate the window to act on!\n");
                }
            } else if (token_equals(value, ARGUMENT_WINDOW_TOGGLE_PIP)) {
                if (acting_window) {
                    window_manager_toggle_window_pip(&g_space_manager, acting_window);
                } else {
                    daemon_fail(rsp, "could not locate the window to act on!\n");
                }
            } else if (!window_manager_toggle_scratchpad_window_by_label(&g_window_manager, value.text)) {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_WINDOW_SUB_LAYER)) {
            if (!acting_window) {
                daemon_fail(rsp, "could not locate the window to act on!\n");
                return;
            }
            struct token value = get_token(&message);
            if (token_equals(value, ARGUMENT_WINDOW_LAYER_BELOW)) {
                if (!window_manager_set_window_layer(acting_window, LAYER_BELOW)) {
                    daemon_fail(rsp, "could not change sub-layer of window with id '%d' due to an error with the scripting-addition.\n", acting_window->id);
                }
            } else if (token_equals(value, ARGUMENT_WINDOW_LAYER_NORMAL)) {
                if (!window_manager_set_window_layer(acting_window, LAYER_NORMAL)) {
                    daemon_fail(rsp, "could not change sub-layer of window with id '%d' due to an error with the scripting-addition.\n", acting_window->id);
                }
            } else if (token_equals(value, ARGUMENT_WINDOW_LAYER_ABOVE)) {
                if (!window_manager_set_window_layer(acting_window, LAYER_ABOVE)) {
                    daemon_fail(rsp, "could not change sub-layer of window with id '%d' due to an error with the scripting-addition.\n", acting_window->id);
                }
            } else if (token_equals(value, ARGUMENT_WINDOW_LAYER_AUTO)) {
                if (!window_manager_set_window_layer(acting_window, LAYER_AUTO)) {
                    daemon_fail(rsp, "could not change sub-layer of window with id '%d' due to an error with the scripting-addition.\n", acting_window->id);
                }
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_WINDOW_OPACITY)) {
            if (!acting_window) {
                daemon_fail(rsp, "could not locate the window to act on!\n");
                return;
            }
            struct token_value value = token_to_value(get_token(&message));
            float opacity;
            if (token_value_to_finite_float(value, &opacity) && in_range_ii(opacity, 0.0f, 1.0f)) {
                if (window_manager_set_opacity(&g_window_manager, acting_window, opacity)) {
                    acting_window->opacity = opacity;
                } else {
                    daemon_fail(rsp, "could not change opacity of window with id '%d' due to an error with the scripting-addition.\n", acting_window->id);
                }
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.token.length, value.token.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_WINDOW_RAISE)) {
            struct selector selector = parse_window_selector(rsp, &message, acting_window, true);
            uint32_t selector_wid = 0;

            if (token_is_valid(selector.token)) {
                if (selector.did_parse && selector.window) {
                    selector_wid = selector.window->id;
                } else {
                    return;
                }
            }

            if (!scripting_addition_order_window(acting_window->id, 1, selector_wid)) {
                daemon_fail(rsp, "could not raise window with id '%d' due to an error with the scripting-addition.\n", acting_window->id);
            }
        } else if (token_equals(command, COMMAND_WINDOW_LOWER)) {
            struct selector selector = parse_window_selector(rsp, &message, acting_window, true);
            uint32_t selector_wid = 0;

            if (token_is_valid(selector.token)) {
                if (selector.did_parse && selector.window) {
                    selector_wid = selector.window->id;
                } else {
                    return;
                }
            }

            if (!scripting_addition_order_window(acting_window->id, -1, selector_wid)) {
                daemon_fail(rsp, "could not lower window with id '%d' due to an error with the scripting-addition.\n", acting_window->id);
            }
        } else if (token_equals(command, COMMAND_WINDOW_SCRATCHPAD)) {
            char *label;
            struct token token = get_token(&message);
            if (token_is_valid(token) && token_equals(token, ARGUMENT_WINDOW_SCRATCHPAD_RECOVER)) {
                window_manager_scratchpad_recover_windows();
            } else if (parse_label(rsp, token, LABEL_WINDOW, &label)) {
                if (label) {
                    if (!window_manager_set_scratchpad_for_window(&g_window_manager, acting_window, label)) {
                        daemon_fail(rsp, "the given scratchpad is already assigned to a different window!\n");
                    }
                } else {
                    if (!window_manager_remove_scratchpad_for_window(&g_window_manager, acting_window, true)) {
                        daemon_fail(rsp, "the selected window was not assigned to a scratchpad!\n");
                    }
                }
            }
        } else {
            daemon_fail(rsp, "unknown command '%.*s' for domain '%.*s'\n", command.length, command.text, domain.length, domain.text);
        }
    }
}
