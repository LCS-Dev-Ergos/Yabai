// `yabai -m display`: --focus, --space and --label. Event loop.

static void handle_domain_display(FILE *rsp, struct token domain, char *message)
{
    TIME_FUNCTION;

    struct token command;
    uint32_t acting_did = display_manager_active_display_id();
    struct selector selector = parse_display_selector(NULL, &message, acting_did, true);

    if (selector.did_parse) {
        acting_did = selector.did;
        command = get_token(&message);
    } else {
        command = selector.token;
    }

    if (!acting_did) {
        daemon_fail(rsp, "could not locate the display to act on!\n");
        return;
    }

    if (token_equals(command, COMMAND_DISPLAY_FOCUS)) {
        struct selector selector = parse_display_selector(rsp, &message, acting_did, false);
        if (selector.did_parse && selector.did) {
            if (acting_did != selector.did) {
                display_manager_focus_display(selector.did, display_space_id(selector.did));
            } else {
                daemon_fail(rsp, "cannot focus an already focused display.\n");
            }
        }
    } else if (token_equals(command, COMMAND_DISPLAY_SPACE)) {
        struct selector selector = parse_space_selector(rsp, &message, display_space_id(acting_did), false);
        if (selector.did_parse && selector.sid) {
            enum space_op_error result = display_manager_focus_space(acting_did, selector.sid);
            if (result == SPACE_OP_ERROR_SAME_DISPLAY) {
                daemon_fail(rsp, "acting display does not contain the given space.\n");
            } else if (result == SPACE_OP_ERROR_DISPLAY_IS_ANIMATING) {
                daemon_fail(rsp, "cannot focus space because the display is in the middle of an animation.\n");
            } else if (result == SPACE_OP_ERROR_IN_MISSION_CONTROL) {
                daemon_fail(rsp, "cannot focus space because mission-control is active.\n");
            } else if (result == SPACE_OP_ERROR_SCRIPTING_ADDITION) {
                daemon_fail(rsp, "cannot focus space due to an error with the scripting-addition.\n");
            }
        }
    } else if (token_equals(command, COMMAND_DISPLAY_LABEL)) {
        char *label;
        if (parse_label(rsp, get_token(&message), LABEL_DISPLAY, &label)) {
            if (label) {
                display_manager_set_label_for_display(&g_display_manager, acting_did, label);
            } else {
                if (!display_manager_remove_label_for_display(&g_display_manager, acting_did)) {
                    daemon_fail(rsp, "the selected display was not associated with a label!\n");
                }
            }
        }
    } else {
        daemon_fail(rsp, "unknown command '%.*s' for domain '%.*s'\n", command.length, command.text, domain.length, domain.text);
    }
}
