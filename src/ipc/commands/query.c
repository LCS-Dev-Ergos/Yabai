// `yabai -m query`: displays, Spaces and windows as JSON. Event loop.

static void handle_domain_query(FILE *rsp, struct token domain, char *message)
{
    TIME_FUNCTION;

    struct token command = get_token(&message);
    if (token_equals(command, COMMAND_QUERY_DISPLAYS)) {
        struct properties properties = parse_properties(rsp, get_token(&message), display_property_val, display_property_str, array_count(display_property_str));
        if (properties.did_error) return;

        struct token option = properties.did_parse ? get_token(&message) : properties.token;
        if (token_equals(option, ARGUMENT_QUERY_DISPLAY)) {
            uint32_t acting_did = display_manager_active_display_id();
            struct selector selector = parse_display_selector(rsp, &message, acting_did, true);

            if (token_is_valid(selector.token)) {
                if (selector.did_parse && selector.did) {
                    acting_did = selector.did;
                } else {
                    return;
                }
            }

            display_serialize(rsp, acting_did, properties.flags);
            fprintf(rsp, "\n");
        } else if (token_equals(option, ARGUMENT_QUERY_SPACE)) {
            uint64_t acting_sid = space_manager_active_space();
            struct selector selector = parse_space_selector(rsp, &message, acting_sid, true);

            if (token_is_valid(selector.token)) {
                if (selector.did_parse && selector.sid) {
                    acting_sid = selector.sid;
                } else {
                    return;
                }
            }

            display_serialize(rsp, space_display_id(acting_sid), properties.flags);
            fprintf(rsp, "\n");
        } else if (token_equals(option, ARGUMENT_QUERY_WINDOW)) {
            struct window *acting_window = window_manager_focused_window(&g_window_manager);
            struct selector selector = parse_window_selector(rsp, &message, acting_window, true);

            if (token_is_valid(selector.token)) {
                if (selector.did_parse && selector.window) {
                    acting_window = selector.window;
                } else {
                    return;
                }
            }

            if (acting_window) {
                display_serialize(rsp, window_display_id(acting_window->id), properties.flags);
                fprintf(rsp, "\n");
            } else {
                daemon_fail(rsp, "could not find window to retrieve display details.\n");
            }
        } else if (token_is_valid(option)) {
            daemon_fail(rsp, "unknown option '%.*s' given to command '%.*s' for domain '%.*s'\n", option.length, option.text, command.length, command.text, domain.length, domain.text);
        } else {
            display_manager_query_displays(rsp, properties.flags);
        }
    } else if (token_equals(command, COMMAND_QUERY_SPACES)) {
        struct properties properties = parse_properties(rsp, get_token(&message), space_property_val, space_property_str, array_count(space_property_str));
        if (properties.did_error) return;

        struct token option = properties.did_parse ? get_token(&message) : properties.token;
        if (token_equals(option, ARGUMENT_QUERY_DISPLAY)) {
            uint32_t acting_did = display_manager_active_display_id();
            struct selector selector = parse_display_selector(rsp, &message, acting_did, true);

            if (token_is_valid(selector.token)) {
                if (selector.did_parse && selector.did) {
                    acting_did = selector.did;
                } else {
                    return;
                }
            }

            if (!space_manager_query_spaces_for_display(rsp, acting_did, properties.flags)) {
                daemon_fail(rsp, "could not retrieve spaces for display.\n");
            }
        } else if (token_equals(option, ARGUMENT_QUERY_SPACE)) {
            uint64_t acting_sid = space_manager_active_space();
            struct selector selector = parse_space_selector(rsp, &message, acting_sid, true);

            if (token_is_valid(selector.token)) {
                if (selector.did_parse && selector.sid) {
                    acting_sid = selector.sid;
                } else {
                    return;
                }
            }

            if (!space_manager_query_space(rsp, acting_sid, properties.flags)) {
                daemon_fail(rsp, "could not retrieve space details.\n");
            }
        } else if (token_equals(option, ARGUMENT_QUERY_WINDOW)) {
            struct window *acting_window = window_manager_focused_window(&g_window_manager);
            struct selector selector = parse_window_selector(rsp, &message, acting_window, true);

            if (token_is_valid(selector.token)) {
                if (selector.did_parse && selector.window) {
                    acting_window = selector.window;
                } else {
                    return;
                }
            }

            if (acting_window) {
                space_manager_query_spaces_for_window(rsp, acting_window, properties.flags);
            } else {
                daemon_fail(rsp, "could not find window to retrieve space details.\n");
            }
        } else if (token_is_valid(option)) {
            daemon_fail(rsp, "unknown option '%.*s' given to command '%.*s' for domain '%.*s'\n", option.length, option.text, command.length, command.text, domain.length, domain.text);
        } else if (!space_manager_query_spaces_for_displays(rsp, properties.flags)) {
            daemon_fail(rsp, "could not retrieve spaces for displays.\n");
        }
    } else if (token_equals(command, COMMAND_QUERY_WINDOWS)) {
        struct properties properties = parse_properties(rsp, get_token(&message), window_property_val, window_property_str, array_count(window_property_str));
        if (properties.did_error) return;

        struct token option = properties.did_parse ? get_token(&message) : properties.token;
        if (token_equals(option, ARGUMENT_QUERY_DISPLAY)) {
            uint32_t acting_did = display_manager_active_display_id();
            struct selector selector = parse_display_selector(rsp, &message, acting_did, true);

            if (token_is_valid(selector.token)) {
                if (selector.did_parse && selector.did) {
                    acting_did = selector.did;
                } else {
                    return;
                }
            }

            window_manager_query_windows_for_display(rsp, acting_did, properties.flags);
        } else if (token_equals(option, ARGUMENT_QUERY_SPACE)) {
            uint64_t acting_sid = space_manager_active_space();
            struct selector selector = parse_space_selector(rsp, &message, acting_sid, true);

            if (token_is_valid(selector.token)) {
                if (selector.did_parse && selector.sid) {
                    acting_sid = selector.sid;
                } else {
                    return;
                }
            }

            window_manager_query_windows_for_spaces(rsp, &acting_sid, 1, properties.flags);
        } else if (token_equals(option, ARGUMENT_QUERY_WINDOW)) {
            struct window *acting_window = window_manager_focused_window(&g_window_manager);
            struct selector selector = parse_window_selector(rsp, &message, acting_window, true);

            if (token_is_valid(selector.token)) {
                if (selector.did_parse && selector.window) {
                    acting_window = selector.window;
                } else {
                    return;
                }
            }

            if (acting_window) {
                window_serialize(rsp, acting_window, properties.flags);
                fprintf(rsp, "\n");
            } else {
                daemon_fail(rsp, "could not retrieve window details.\n");
            }
        } else if (token_is_valid(option)) {
            daemon_fail(rsp, "unknown option '%.*s' given to command '%.*s' for domain '%.*s'\n", option.length, option.text, command.length, command.text, domain.length, domain.text);
        } else {
            window_manager_query_windows_for_displays(rsp, properties.flags);
        }
    } else {
        daemon_fail(rsp, "unknown command '%.*s' for domain '%.*s'\n", command.length, command.text, domain.length, domain.text);
    }
}
