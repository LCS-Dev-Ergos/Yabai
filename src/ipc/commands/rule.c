// `yabai -m rule`: parsing, adding, applying and removing window rules.
// Event loop.

static bool parse_rule(FILE *rsp, char **message, struct rule *rule, struct token token)
{
    TIME_FUNCTION;

    char *unsupported_exclusion = NULL;
    bool did_parse = true;
    bool has_filter = false;

    for (; token_is_valid(token); token = get_token(message)) {
        char *key = NULL;
        char *value = NULL;
        bool exclusion = false;
        parse_key_value_pair(token.text, &key, &value, &exclusion);

        if (!key || !value) {
            daemon_fail(rsp, "invalid key-value pair '%s'\n", token.text);
            did_parse = false;
            continue;
        }

        if (string_equals(key, ARGUMENT_RULE_KEY_LABEL)) {
            if (exclusion) unsupported_exclusion = key;
            rule->label = string_copy(value);
        } else if (string_equals(key, ARGUMENT_RULE_KEY_SCRATCHPAD)) {
            if (exclusion) unsupported_exclusion = key;

            bool valid = true;
            for (int i = 0; i < array_count(reserved_window_identifiers); ++i) {
                if (string_equals(value, reserved_window_identifiers[i])) {
                    valid = false;
                    break;
                }
            }

            if (valid) {
                rule->effects.scratchpad = string_copy(value);
                rule->effects.manage = RULE_PROP_OFF;
            } else {
                daemon_fail(rsp, "invalid value '%s' for key '%s'\n", value, key);
                did_parse = false;
            }
        } else if (string_equals(key, ARGUMENT_RULE_KEY_APP)) {
            has_filter = true;
            rule->app = string_copy(value);
            if (exclusion) rule_set_flag(rule, RULE_APP_EXCLUDE);
            if (regcomp(&rule->app_regex, value, REG_EXTENDED) == 0) {
                rule_set_flag(rule, RULE_APP_VALID);
            } else {
                daemon_fail(rsp, "invalid regex pattern '%s' for key '%s'\n", value, key);
                did_parse = false;
            }
        } else if (string_equals(key, ARGUMENT_RULE_KEY_TITLE)) {
            has_filter = true;
            rule->title = string_copy(value);
            if (exclusion) rule_set_flag(rule, RULE_TITLE_EXCLUDE);
            if (regcomp(&rule->title_regex, value, REG_EXTENDED) == 0) {
                rule_set_flag(rule, RULE_TITLE_VALID);
            } else {
                daemon_fail(rsp, "invalid regex pattern '%s' for key '%s'\n", value, key);
                did_parse = false;
            }
        } else if (string_equals(key, ARGUMENT_RULE_KEY_ROLE)) {
            has_filter = true;
            rule->role = string_copy(value);
            if (exclusion) rule_set_flag(rule, RULE_ROLE_EXCLUDE);
            if (regcomp(&rule->role_regex, value, REG_EXTENDED) == 0) {
                rule_set_flag(rule, RULE_ROLE_VALID);
            } else {
                daemon_fail(rsp, "invalid regex pattern '%s' for key '%s'\n", value, key);
                did_parse = false;
            }
        } else if (string_equals(key, ARGUMENT_RULE_KEY_SUBROLE)) {
            has_filter = true;
            rule->subrole = string_copy(value);
            if (exclusion) rule_set_flag(rule, RULE_SUBROLE_EXCLUDE);
            if (regcomp(&rule->subrole_regex, value, REG_EXTENDED) == 0) {
                rule_set_flag(rule, RULE_SUBROLE_VALID);
            } else {
                daemon_fail(rsp, "invalid regex pattern '%s' for key '%s'\n", value, key);
                did_parse = false;
            }
        } else if (string_equals(key, ARGUMENT_RULE_KEY_DISPLAY)) {
            if (exclusion) unsupported_exclusion = key;

            if (value[0] == ARGUMENT_RULE_VALUE_SPACE) {
                ++value;
                rule_effects_set_flag(&rule->effects, RULE_FOLLOW_SPACE);
            }

            struct selector selector = parse_display_selector(rsp, &value, display_manager_active_display_id(), false);
            if (selector.did_parse && selector.did) {
                rule->effects.did = selector.did;
            } else {
                did_parse = false;
            }
        } else if (string_equals(key, ARGUMENT_RULE_KEY_SPACE)) {
            if (exclusion) unsupported_exclusion = key;

            if (value[0] == ARGUMENT_RULE_VALUE_SPACE) {
                ++value;
                rule_effects_set_flag(&rule->effects, RULE_FOLLOW_SPACE);
            }

            struct selector selector = parse_space_selector(rsp, &value, space_manager_active_space(), false);
            if (selector.did_parse && selector.sid) {
                rule->effects.sid = selector.sid;
            } else {
                did_parse = false;
            }
        } else if (string_equals(key, ARGUMENT_RULE_KEY_GRID)) {
            if (exclusion) unsupported_exclusion = key;

            if (!parse_grid(value, rule->effects.grid)) {
                daemon_fail(rsp, "invalid value '%s' for key '%s'\n", value, key);
                did_parse = false;
            }
        } else if (string_equals(key, ARGUMENT_RULE_KEY_OPACITY)) {
            if (exclusion) unsupported_exclusion = key;

            if ((sscanf(value, "%f", &rule->effects.opacity) == 1) && (in_range_ii(rule->effects.opacity, 0.0f, 1.0f))) {
                rule_effects_set_flag(&rule->effects, RULE_OPACITY);
            } else {
                daemon_fail(rsp, "invalid value '%s' for key '%s'\n", value, key);
                did_parse = false;
            }
        } else if (string_equals(key, ARGUMENT_RULE_KEY_MANAGE)) {
            if (exclusion) unsupported_exclusion = key;

            if (string_equals(value, ARGUMENT_COMMON_VAL_ON)) {
                rule->effects.manage = RULE_PROP_ON;
            } else if (string_equals(value, ARGUMENT_COMMON_VAL_OFF)) {
                rule->effects.manage = RULE_PROP_OFF;
            } else {
                daemon_fail(rsp, "invalid value '%s' for key '%s'\n", value, key);
                did_parse = false;
            }
        } else if (string_equals(key, ARGUMENT_RULE_KEY_STICKY)) {
            if (exclusion) unsupported_exclusion = key;

            if (string_equals(value, ARGUMENT_COMMON_VAL_ON)) {
                rule->effects.sticky = RULE_PROP_ON;
            } else if (string_equals(value, ARGUMENT_COMMON_VAL_OFF)) {
                rule->effects.sticky = RULE_PROP_OFF;
            } else {
                daemon_fail(rsp, "invalid value '%s' for key '%s'\n", value, key);
                did_parse = false;
            }
        } else if (string_equals(key, ARGUMENT_RULE_KEY_MFF)) {
            if (exclusion) unsupported_exclusion = key;

            if (string_equals(value, ARGUMENT_COMMON_VAL_ON)) {
                rule->effects.mff = RULE_PROP_ON;
            } else if (string_equals(value, ARGUMENT_COMMON_VAL_OFF)) {
                rule->effects.mff = RULE_PROP_OFF;
            } else {
                daemon_fail(rsp, "invalid value '%s' for key '%s'\n", value, key);
                did_parse = false;
            }
        } else if (string_equals(key, ARGUMENT_RULE_KEY_SUB_LAYER)) {
            if (exclusion) unsupported_exclusion = key;

            if (string_equals(value, ARGUMENT_WINDOW_LAYER_BELOW)) {
                rule->effects.layer = LAYER_BELOW;
                rule_effects_set_flag(&rule->effects, RULE_LAYER);
            } else if (string_equals(value, ARGUMENT_WINDOW_LAYER_NORMAL)) {
                rule->effects.layer = LAYER_NORMAL;
                rule_effects_set_flag(&rule->effects, RULE_LAYER);
            } else if (string_equals(value, ARGUMENT_WINDOW_LAYER_ABOVE)) {
                rule->effects.layer = LAYER_ABOVE;
                rule_effects_set_flag(&rule->effects, RULE_LAYER);
            } else if (string_equals(value, ARGUMENT_WINDOW_LAYER_AUTO)) {
                rule->effects.layer = LAYER_AUTO;
                rule_effects_set_flag(&rule->effects, RULE_LAYER);
            } else {
                daemon_fail(rsp, "invalid value '%s' for key '%s'\n", value, key);
                did_parse = false;
            }
        } else if (string_equals(key, ARGUMENT_RULE_KEY_FULLSCR)) {
            if (exclusion) unsupported_exclusion = key;

            if (string_equals(value, ARGUMENT_COMMON_VAL_ON)) {
                rule->effects.fullscreen = RULE_PROP_ON;
            } else if (string_equals(value, ARGUMENT_COMMON_VAL_OFF)) {
                rule->effects.fullscreen = RULE_PROP_OFF;
            } else {
                daemon_fail(rsp, "invalid value '%s' for key '%s'\n", value, key);
                did_parse = false;
            }
        } else {
            daemon_fail(rsp, "unknown key '%s'\n", key);
            did_parse = false;
        }
    }

    if (!has_filter) {
        daemon_fail(rsp, "missing required key-value pair 'app[!]=..' or 'title[!]=..'\n");
        did_parse = false;
    }

    if (unsupported_exclusion) {
        daemon_fail(rsp, "unsupported token '!' (exclusion) given for key '%s'\n", unsupported_exclusion);
        did_parse = false;
    }

    return did_parse;
}

static void handle_domain_rule(FILE *rsp, struct token domain, char *message)
{
    TIME_FUNCTION;

    struct token command = get_token(&message);
    if (token_equals(command, COMMAND_RULE_ADD)) {
        struct rule rule = {0};

        struct token token = get_token(&message);
        if (token_equals(token, ARGUMENT_RULE_ONE_SHOT)) {
            rule_set_flag(&rule, RULE_ONE_SHOT);
            token = get_token(&message);
        }

        if (parse_rule(rsp, &message, &rule, token)) {
            rule_add(&rule);
        } else {
            rule_destroy(&rule);
        }
    } else if (token_equals(command, COMMAND_RULE_APPLY)) {
        struct token_value value = token_to_value(get_token(&message));
        if (value.type == TOKEN_TYPE_INT) {
            if (!rule_reapply_by_index(value.int_value)) {
                daemon_fail(rsp, "rule with index '%d' not found.\n", value.int_value);
            }
        } else if (value.type == TOKEN_TYPE_STRING) {
            if (!rule_reapply_by_label(value.string_value)) {
                struct rule rule = {0};
                if (parse_rule(rsp, &message, &rule, value.token)) {
                    rule_apply(&rule);
                }
                rule_destroy(&rule);
            }
        } else if (value.type == TOKEN_TYPE_INVALID) {
            rule_reapply_all();
        } else {
            daemon_fail(rsp, "value '%.*s' is not a valid option for RULE_SEL\n", value.token.length, value.token.text);
        }
    } else if (token_equals(command, COMMAND_RULE_REM)) {
        struct token_value value = token_to_value(get_token(&message));
        if (value.type == TOKEN_TYPE_INT) {
            if (!rule_remove_by_index(value.int_value)) {
                daemon_fail(rsp, "rule with index '%d' not found.\n", value.int_value);
            }
        } else if (value.type == TOKEN_TYPE_STRING) {
            if (!rule_remove_by_label(value.string_value)) {
                daemon_fail(rsp, "rule with label '%s' not found.\n", value.string_value);
            }
        } else {
            daemon_fail(rsp, "value '%.*s' is not a valid option for RULE_SEL\n", value.token.length, value.token.text);
        }
    } else if (token_equals(command, COMMAND_RULE_LS)) {
        window_manager_query_window_rules(rsp);
    } else {
        daemon_fail(rsp, "unknown command '%.*s' for domain '%.*s'\n", command.length, command.text, domain.length, domain.text);
    }
}
