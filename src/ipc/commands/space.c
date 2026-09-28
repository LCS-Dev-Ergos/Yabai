// `yabai -m space`: focus and navigation (the latter in navigation/), creation
// and destruction, moves between and across displays, labels, layout, padding,
// gaps and the operations on its tree. Event loop.

static void handle_domain_space(FILE *rsp, struct token domain, char *message)
{
    TIME_FUNCTION;

    char *cursor = message;
    bool navigate = token_equals(get_token(&cursor), COMMAND_SPACE_NAVIGATE);
    struct token command;
    uint64_t acting_sid = navigate ? space_navigation_active_space() : space_manager_active_space();
    struct selector selector = parse_space_selector(NULL, &message, acting_sid, true);

    if (selector.did_parse) {
        acting_sid = selector.sid;
        command = get_token(&message);
    } else {
        command = selector.token;
    }

    if (!acting_sid) {
        daemon_fail(rsp, "could not locate the space to act on!\n");
        return;
    }

    for (; token_is_valid(command); command = get_token(&message)) {
        if (token_equals(command, COMMAND_SPACE_NAVIGATE)) {
            space_navigation_command(rsp, &message);
        } else if (token_equals(command, COMMAND_SPACE_FOCUS)) {
            struct selector selector = parse_space_selector(rsp, &message, acting_sid, false);
            if (selector.did_parse && selector.sid) {
                enum space_op_error result = space_manager_focus_space(selector.sid);
                if (result == SPACE_OP_ERROR_SAME_SPACE) {
                    daemon_fail(rsp, "cannot focus an already focused space.\n");
                } else if (result == SPACE_OP_ERROR_DISPLAY_IS_ANIMATING) {
                    daemon_fail(rsp, "cannot focus space because the display is in the middle of an animation.\n");
                } else if (result == SPACE_OP_ERROR_IN_MISSION_CONTROL) {
                    daemon_fail(rsp, "cannot focus space because mission-control is active.\n");
                } else if (result == SPACE_OP_ERROR_SCRIPTING_ADDITION) {
                    daemon_fail(rsp, "cannot focus space due to an error with the scripting-addition.\n");
                }
            }
        } else if (token_equals(command, COMMAND_SPACE_SWITCH)) {
            struct selector selector = parse_space_selector(rsp, &message, acting_sid, false);
            if (selector.did_parse && selector.sid) {
                enum space_op_error result = space_manager_switch_space(selector.sid);
                if (result == SPACE_OP_ERROR_SAME_SPACE) {
                    daemon_fail(rsp, "cannot focus an already focused space.\n");
                } else if (result == SPACE_OP_ERROR_DISPLAY_IS_ANIMATING) {
                    daemon_fail(rsp, "cannot focus space because the display is in the middle of an animation.\n");
                } else if (result == SPACE_OP_ERROR_IN_MISSION_CONTROL) {
                    daemon_fail(rsp, "cannot focus space because mission-control is active.\n");
                } else if (result == SPACE_OP_ERROR_SCRIPTING_ADDITION) {
                    daemon_fail(rsp, "cannot focus space due to an error with the scripting-addition.\n");
                }
            }
        } else if (token_equals(command, COMMAND_SPACE_MOVE)) {
            struct selector selector = parse_space_selector(rsp, &message, acting_sid, false);
            if (selector.did_parse && selector.sid) {
                enum space_op_error result = space_manager_move_space_to_space(acting_sid, selector.sid);
                if (result == SPACE_OP_ERROR_SAME_SPACE) {
                    daemon_fail(rsp, "cannot move space to itself.\n");
                } else if (result == SPACE_OP_ERROR_SAME_DISPLAY) {
                    daemon_fail(rsp, "cannot move space across display boundaries. use --display instead.\n");
                } else if (result == SPACE_OP_ERROR_DISPLAY_IS_ANIMATING) {
                    daemon_fail(rsp, "cannot move space because the display is in the middle of an animation.\n");
                } else if (result == SPACE_OP_ERROR_IN_MISSION_CONTROL) {
                    daemon_fail(rsp, "cannot move space because mission-control is active.\n");
                } else if (result == SPACE_OP_ERROR_SCRIPTING_ADDITION) {
                    daemon_fail(rsp, "cannot move space due to an error with the scripting-addition.\n");
                }
            }
        } else if (token_equals(command, COMMAND_SPACE_SWAP)) {
            struct selector selector = parse_space_selector(rsp, &message, acting_sid, false);
            if (selector.did_parse && selector.sid) {
                enum space_op_error result = space_manager_swap_space_with_space(acting_sid, selector.sid);
                if (result == SPACE_OP_ERROR_SAME_SPACE) {
                    daemon_fail(rsp, "cannot swap space with itself.\n");
                } else if (result == SPACE_OP_ERROR_DISPLAY_IS_ANIMATING) {
                    daemon_fail(rsp, "cannot swap space because the display is in the middle of an animation.\n");
                } else if (result == SPACE_OP_ERROR_IN_MISSION_CONTROL) {
                    daemon_fail(rsp, "cannot swap space because mission-control is active.\n");
                } else if (result == SPACE_OP_ERROR_SCRIPTING_ADDITION) {
                    daemon_fail(rsp, "cannot swap space due to an error with the scripting-addition.\n");
                }
            }
        } else if (token_equals(command, COMMAND_SPACE_DISPLAY)) {
            struct selector selector = parse_display_selector(rsp, &message, display_manager_active_display_id(), false);
            if (selector.did_parse && selector.did) {
                enum space_op_error result = space_manager_move_space_to_display(&g_space_manager, acting_sid, selector.did);
                if (result == SPACE_OP_ERROR_MISSING_SRC) {
                    daemon_fail(rsp, "could not locate the space to act on.\n");
                } else if (result == SPACE_OP_ERROR_MISSING_DST) {
                    daemon_fail(rsp, "could not locate the active space of the given display.\n");
                } else if (result == SPACE_OP_ERROR_INVALID_SRC) {
                    daemon_fail(rsp, "acting space is the last user-space on the source display and cannot be moved.\n");
                } else if (result == SPACE_OP_ERROR_INVALID_DST) {
                    daemon_fail(rsp, "acting space is already located on the given display.\n");
                } else if (result == SPACE_OP_ERROR_DISPLAY_IS_ANIMATING) {
                    daemon_fail(rsp, "cannot send space to display because it is in the middle of an animation.\n");
                } else if (result == SPACE_OP_ERROR_IN_MISSION_CONTROL) {
                    daemon_fail(rsp, "cannot send space to display because mission-control is active.\n");
                } else if (result == SPACE_OP_ERROR_SCRIPTING_ADDITION) {
                    daemon_fail(rsp, "cannot send space to display due to an error with the scripting-addition.\n");
                }
            }
        } else if (token_equals(command, COMMAND_SPACE_CREATE)) {
            struct selector selector = parse_display_selector(rsp, &message, display_manager_active_display_id(), true);

            if (token_is_valid(selector.token)) {
                if (selector.did_parse && selector.did) {
                    acting_sid = display_space_id(selector.did);
                } else {
                    return;
                }
            }

            enum space_op_error result = space_manager_add_space(acting_sid);
            if (result == SPACE_OP_ERROR_MISSING_SRC) {
                daemon_fail(rsp, "could not locate the space to act on.\n");
            } else if (result == SPACE_OP_ERROR_DISPLAY_IS_ANIMATING) {
                daemon_fail(rsp, "cannot create space because the display is in the middle of an animation.\n");
            } else if (result == SPACE_OP_ERROR_IN_MISSION_CONTROL) {
                daemon_fail(rsp, "cannot create space because mission-control is active.\n");
            } else if (result == SPACE_OP_ERROR_SCRIPTING_ADDITION) {
                daemon_fail(rsp, "cannot create space due to an error with the scripting-addition.\n");
            }
        } else if (token_equals(command, COMMAND_SPACE_DESTROY)) {
            struct selector selector = parse_space_selector(rsp, &message, acting_sid, true);

            if (token_is_valid(selector.token)) {
                if (selector.did_parse && selector.sid) {
                    acting_sid = selector.sid;
                } else {
                    return;
                }
            }

            enum space_op_error result = space_manager_destroy_space(acting_sid);
            if (result == SPACE_OP_ERROR_MISSING_SRC) {
                daemon_fail(rsp, "could not locate the space to act on.\n");
            } else if (result == SPACE_OP_ERROR_INVALID_SRC) {
                daemon_fail(rsp, "acting space is the last user-space on the source display and cannot be destroyed.\n");
            } else if (result == SPACE_OP_ERROR_INVALID_TYPE) {
                daemon_fail(rsp, "cannot destroy a macOS fullscreen space.\n");
            } else if (result == SPACE_OP_ERROR_DISPLAY_IS_ANIMATING) {
                daemon_fail(rsp, "cannot destroy space because the display is in the middle of an animation.\n");
            } else if (result == SPACE_OP_ERROR_IN_MISSION_CONTROL) {
                daemon_fail(rsp, "cannot destroy space because mission-control is active.\n");
            } else if (result == SPACE_OP_ERROR_SCRIPTING_ADDITION) {
                daemon_fail(rsp, "cannot destroy space due to an error with the scripting-addition.\n");
            }
        } else if (token_equals(command, COMMAND_SPACE_EQUALIZE)) {
            struct token value = get_token(&message);
            if (!token_is_valid(value)) {
                if (!space_manager_equalize_space(&g_space_manager, acting_sid, SPLIT_X | SPLIT_Y)) {
                    daemon_fail(rsp, "cannot equalize a non-managed space.\n");
                }
            } else if (token_equals(value, ARGUMENT_COMMON_VAL_AXIS_X)) {
                if (!space_manager_equalize_space(&g_space_manager, acting_sid, SPLIT_X)) {
                    daemon_fail(rsp, "cannot equalize a non-managed space.\n");
                }
            } else if (token_equals(value, ARGUMENT_COMMON_VAL_AXIS_Y)) {
                if (!space_manager_equalize_space(&g_space_manager, acting_sid, SPLIT_Y)) {
                    daemon_fail(rsp, "cannot equalize a non-managed space.\n");
                }
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_SPACE_BALANCE)) {
            struct token value = get_token(&message);
            if (!token_is_valid(value)) {
                if (!space_manager_balance_space(&g_space_manager, acting_sid, SPLIT_X | SPLIT_Y)) {
                    daemon_fail(rsp, "cannot balance a non-managed space.\n");
                }
            } else if (token_equals(value, ARGUMENT_COMMON_VAL_AXIS_X)) {
                if (!space_manager_balance_space(&g_space_manager, acting_sid, SPLIT_X)) {
                    daemon_fail(rsp, "cannot balance a non-managed space.\n");
                }
            } else if (token_equals(value, ARGUMENT_COMMON_VAL_AXIS_Y)) {
                if (!space_manager_balance_space(&g_space_manager, acting_sid, SPLIT_Y)) {
                    daemon_fail(rsp, "cannot balance a non-managed space.\n");
                }
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_SPACE_MIRROR)) {
            struct token value = get_token(&message);
            if (token_equals(value, ARGUMENT_COMMON_VAL_AXIS_X)) {
                if (!space_manager_mirror_space(&g_space_manager, acting_sid, SPLIT_X)) {
                    daemon_fail(rsp, "cannot mirror a non-managed space.\n");
                }
            } else if (token_equals(value, ARGUMENT_COMMON_VAL_AXIS_Y)) {
                if (!space_manager_mirror_space(&g_space_manager, acting_sid, SPLIT_Y)) {
                    daemon_fail(rsp, "cannot mirror a non-managed space.\n");
                }
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_SPACE_ROTATE)) {
            struct token value = get_token(&message);
            if (token_equals(value, ARGUMENT_SPACE_ROTATE_90)) {
                if (!space_manager_rotate_space(&g_space_manager, acting_sid, 90)) {
                    daemon_fail(rsp, "cannot rotate a non-managed space.\n");
                }
            } else if (token_equals(value, ARGUMENT_SPACE_ROTATE_180)) {
                if (!space_manager_rotate_space(&g_space_manager, acting_sid, 180)) {
                    daemon_fail(rsp, "cannot rotate a non-managed space.\n");
                }
            } else if (token_equals(value, ARGUMENT_SPACE_ROTATE_270)) {
                if (!space_manager_rotate_space(&g_space_manager, acting_sid, 270)) {
                    daemon_fail(rsp, "cannot rotate a non-managed space.\n");
                }
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_SPACE_PADDING)) {
            int t, b, l, r;
            char type[MAXLEN];
            struct token value = get_token(&message);
            if ((sscanf(value.text, ARGUMENT_SPACE_PADDING, type, &t, &b, &l, &r) == 5)) {
                if (!space_manager_set_padding_for_space(&g_space_manager, acting_sid, parse_value_type(type), t, b, l, r)) {
                    daemon_fail(rsp, "cannot set padding for a non-managed space.\n");
                }
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_SPACE_GAP)) {
            int gap;
            char type[MAXLEN];
            struct token value = get_token(&message);
            if ((sscanf(value.text, ARGUMENT_SPACE_GAP, type, &gap) == 2)) {
                if (!space_manager_set_gap_for_space(&g_space_manager, acting_sid, parse_value_type(type), gap)) {
                    daemon_fail(rsp, "cannot set gap for a non-managed space.\n");
                }
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_SPACE_TOGGLE)) {
            struct token value = get_token(&message);
            if (token_equals(value, ARGUMENT_SPACE_TGL_PADDING)) {
                if (!space_manager_toggle_padding_for_space(&g_space_manager, acting_sid)) {
                    daemon_fail(rsp, "cannot toggle padding for a non-managed space.\n");
                }
            } else if (token_equals(value, ARGUMENT_SPACE_TGL_GAP)) {
                if (!space_manager_toggle_gap_for_space(&g_space_manager, acting_sid)) {
                    daemon_fail(rsp, "cannot toggle gap for a non-managed space.\n");
                }
            } else if (token_equals(value, ARGUMENT_SPACE_TGL_MC)) {
                space_manager_toggle_mission_control(acting_sid);
            } else if (token_equals(value, ARGUMENT_SPACE_TGL_SD)) {
                space_manager_toggle_show_desktop(acting_sid);
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_SPACE_LAYOUT)) {
            struct token value = get_token(&message);
            if (token_equals(value, ARGUMENT_SPACE_LAYOUT_BSP)) {
                if (space_is_user(acting_sid)) {
                    space_manager_set_layout_for_space(&g_space_manager, acting_sid, VIEW_BSP);
                } else {
                    daemon_fail(rsp, "cannot set layout for a macOS fullscreen space!\n");
                }
            } else if (token_equals(value, ARGUMENT_SPACE_LAYOUT_STACK)) {
                if (space_is_user(acting_sid)) {
                    space_manager_set_layout_for_space(&g_space_manager, acting_sid, VIEW_STACK);
                } else {
                    daemon_fail(rsp, "cannot set layout for a macOS fullscreen space!\n");
                }
            } else if (token_equals(value, ARGUMENT_SPACE_LAYOUT_FLT)) {
                if (space_is_user(acting_sid)) {
                    space_manager_set_layout_for_space(&g_space_manager, acting_sid, VIEW_FLOAT);
                } else {
                    daemon_fail(rsp, "cannot set layout for a macOS fullscreen space!\n");
                }
            } else {
                daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
            }
        } else if (token_equals(command, COMMAND_SPACE_LABEL)) {
            char *label;
            if (parse_label(rsp, get_token(&message), LABEL_SPACE, &label)) {
                if (label) {
                    space_manager_set_label_for_space(&g_space_manager, acting_sid, label);
                } else {
                    if (!space_manager_remove_label_for_space(&g_space_manager, acting_sid)) {
                        daemon_fail(rsp, "the selected space was not associated with a label!\n");
                    }
                }
            }
        } else {
            daemon_fail(rsp, "unknown command '%.*s' for domain '%.*s'\n", command.length, command.text, domain.length, domain.text);
        }
    }
}
