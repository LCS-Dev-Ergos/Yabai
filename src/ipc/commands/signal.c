// `yabai -m signal`: adding, listing and removing signal subscriptions.
// Event loop.

// A key given again replaces its value, as it always has; the text and the
// compiled pattern it replaces are released.
static void parse_signal_text(char **text, char *value)
{
    free(*text);
    *text = string_copy(value);
}

static bool parse_signal_pattern(char **text, regex_t *regex, bool *valid, bool *exclude, char *value, bool exclusion)
{
    if (*valid) regfree(regex);
    parse_signal_text(text, value);

    *exclude = exclusion;
    *valid = regcomp(regex, value, REG_EXTENDED) == 0;
    return *valid;
}

static void handle_domain_signal(FILE *rsp, struct token domain, char *message)
{
    TIME_FUNCTION;

    struct token command = get_token(&message);
    if (token_equals(command, COMMAND_SIGNAL_ADD)) {
        char *unsupported_exclusion = NULL;
        bool did_parse = true;
        bool has_command = false;
        bool has_signal_type = false;
        enum signal_type signal_type = SIGNAL_TYPE_UNKNOWN;
        struct signal signal = {0};

        for (struct token token = get_token(&message); token_is_valid(token); token = get_token(&message)) {
            char *key = NULL;
            char *value = NULL;
            bool exclusion = false;
            parse_key_value_pair(token.text, &key, &value, &exclusion);

            if (!key || !value) {
                daemon_fail(rsp, "invalid key-value pair '%s'\n", token.text);
                did_parse = false;
                continue;
            }

            if (string_equals(key, ARGUMENT_SIGNAL_KEY_LABEL)) {
                if (exclusion) unsupported_exclusion = key;
                parse_signal_text(&signal.label, value);
            } else if (string_equals(key, ARGUMENT_SIGNAL_KEY_APP)) {
                if (!parse_signal_pattern(&signal.app, &signal.app_regex, &signal.app_regex_valid,
                                          &signal.app_regex_exclude, value, exclusion)) {
                    daemon_fail(rsp, "invalid regex pattern '%s' for key '%s'\n", value, key);
                    did_parse = false;
                }
            } else if (string_equals(key, ARGUMENT_SIGNAL_KEY_TITLE)) {
                if (!parse_signal_pattern(&signal.title, &signal.title_regex, &signal.title_regex_valid,
                                          &signal.title_regex_exclude, value, exclusion)) {
                    daemon_fail(rsp, "invalid regex pattern '%s' for key '%s'\n", value, key);
                    did_parse = false;
                }
            } else if (string_equals(key, ARGUMENT_SIGNAL_KEY_ACTIVE)) {
                if (exclusion) unsupported_exclusion = key;

                if (string_equals(value, ARGUMENT_SIGNAL_VALUE_YES)) {
                    signal.active = SIGNAL_PROP_YES;
                } else if (string_equals(value, ARGUMENT_SIGNAL_VALUE_NO)) {
                    signal.active = SIGNAL_PROP_NO;
                } else {
                    daemon_fail(rsp, "invalid value '%s' for key '%s'\n", value, key);
                    did_parse = false;
                }
            } else if (string_equals(key, ARGUMENT_SIGNAL_KEY_ACTION)) {
                if (exclusion) unsupported_exclusion = key;

                has_command = true;
                parse_signal_text(&signal.command, value);
            } else if (string_equals(key, ARGUMENT_SIGNAL_KEY_EVENT)) {
                if (exclusion) unsupported_exclusion = key;

                has_signal_type = true;
                signal_type = signal_type_from_string(value);
                if (signal_type == SIGNAL_TYPE_UNKNOWN) {
                    daemon_fail(rsp, "invalid value '%s' for key '%s'\n", value, key);
                    did_parse = false;
                }
            } else {
                daemon_fail(rsp, "unknown key '%s'\n", key);
                did_parse = false;
            }
        }

        if (!has_signal_type) {
            daemon_fail(rsp, "missing required key-value pair 'event=..'\n");
            did_parse = false;
        }

        if (!has_command) {
            daemon_fail(rsp, "missing required key-value pair 'action=..'\n");
            did_parse = false;
        }

        if (unsupported_exclusion) {
            daemon_fail(rsp, "unsupported token '!' (exclusion) given for key '%s'\n", unsupported_exclusion);
            did_parse = false;
        }

        if (did_parse) {
            event_signal_add(signal_type, &signal);
        } else {
            event_signal_destroy(&signal);
        }
    } else if (token_equals(command, COMMAND_SIGNAL_REM)) {
        struct token_value value = token_to_value(get_token(&message));
        if (value.type == TOKEN_TYPE_INT) {
            if (!event_signal_remove_by_index(value.int_value)) {
                daemon_fail(rsp, "signal with index '%d' not found.\n", value.int_value);
            }
        } else if (value.type == TOKEN_TYPE_STRING) {
            if (!event_signal_remove(value.string_value)) {
                daemon_fail(rsp, "signal with label '%s' not found.\n", value.string_value);
            }
        } else {
            daemon_fail(rsp, "value '%.*s' is not a valid option for SIGNAL_SEL\n", value.token.length, value.token.text);
        }
    } else if (token_equals(command, COMMAND_SIGNAL_LS)) {
        event_signal_list(rsp);
    } else {
        daemon_fail(rsp, "unknown command '%.*s' for domain '%.*s'\n", command.length, command.text, domain.length, domain.text);
    }
}
