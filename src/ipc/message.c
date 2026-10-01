// The command language every domain in commands/ parses: tokens, values,
// properties and the selectors of displays, Spaces and windows. Its
// vocabulary is in message.h.

static const int token_char_int_table[] =
{
    ['0'] = 0x0, ['1'] = 0x1,
    ['2'] = 0x2, ['3'] = 0x3,
    ['4'] = 0x4, ['5'] = 0x5,
    ['6'] = 0x6, ['7'] = 0x7,
    ['8'] = 0x8, ['9'] = 0x9,
    ['a'] = 0xA, ['A'] = 0xA,
    ['b'] = 0xB, ['B'] = 0xB,
    ['c'] = 0xC, ['C'] = 0xC,
    ['d'] = 0xD, ['D'] = 0xD,
    ['e'] = 0xE, ['E'] = 0xE,
    ['f'] = 0xF, ['F'] = 0xF,
};

struct token get_token(char **message)
{
    struct token token;

    token.text = *message;
    while (**message) {
        ++(*message);
    }
    token.length = *message - token.text;

    if ((*message)[0] == '\0' && (*message)[1] != '\0') {
        ++(*message);
    } else {
        // NOTE(asmvik): don't go past the null-terminator
    }

    return token;
}

static bool token_prefix(struct token token, char *match)
{
    char *at = match;
    for (int i = 0; i < token.length; ++i, ++at) {
        if (*at == 0)             return true;
        if (token.text[i] != *at) return false;
    }
    return *at == 0;
}

bool token_equals(struct token token, char *match)
{
    char *at = match;
    for (int i = 0; i < token.length; ++i, ++at) {
        if ((*at == 0) || (token.text[i] != *at)) {
            return false;
        }
    }
    return *at == 0;
}

static inline bool token_is_valid(struct token token)
{
    return token.text && token.length > 0;
}

static bool token_is_positive_integer(struct token token, int *value)
{
    *value = 0;

    for (int i = 0; i < token.length; ++i) {
        char c = token.text[i];
        if (!(c >= '0' && c <= '9')) {
            return false;
        }
        if (*value > (INT_MAX - token_char_int_table[(int)c]) / 10) {
            return false;
        }
        *value = *value * 10 + token_char_int_table[(int)c];
    }

    return true;
}

static bool token_is_hexadecimal(struct token token, uint32_t *value)
{
    *value = 0;

    if (token.length <= 2 || token.length > 10) return false;

    if (!(token.text[0] == '0' &&
         (token.text[1] == 'x' ||
          token.text[1] == 'X'))) {
        return false;
    }

    for (int i = 2; i < token.length; ++i) {
        char c = token.text[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) {
            return false;
        }
        *value = *value * 16 + (uint32_t)token_char_int_table[(int)c];
    }

    return true;
}

static bool token_is_float(struct token token, float *value)
{
    char *end = NULL;
    float v = strtof(token.text, &end);

    if (!end || *end) {
        *value = 0.0f;
        return false;
    } else {
        *value = v;
        return true;
    }
}

struct token_value token_to_value(struct token token)
{
    struct token_value value = { .token = token, .type = TOKEN_TYPE_INVALID };

    if (token_is_valid(token)) {
        if (token_is_positive_integer(token, &value.int_value)) {
            value.type = TOKEN_TYPE_INT;
        } else if (token_is_hexadecimal(token, &value.u32_value)) {
            value.type = TOKEN_TYPE_U32;
        } else if (token_is_float(token, &value.float_value)) {
            value.type = TOKEN_TYPE_FLOAT;
        } else if ((value.string_value = token.text)) {
            value.type = TOKEN_TYPE_STRING;
        } else {
            value.type = TOKEN_TYPE_UNKNOWN;
        }
    }

    return value;
}

// A decimal argument: a decimal or a whole number, which token_to_value types
// as an integer. strtof also reads nan and inf, which no setting can hold: a
// NaN passes every range check written as comparisons.
static bool token_value_to_finite_float(struct token_value value, float *result)
{
    if (value.type == TOKEN_TYPE_INT) {
        *result = (float) value.int_value;
        return true;
    }

    if (value.type == TOKEN_TYPE_FLOAT && isfinite(value.float_value)) {
        *result = value.float_value;
        return true;
    }

    return false;
}

// rows:cols:start-x:start-y:width:height, as window --grid and the grid rule
// take it. A grid has at least one row and one column, and no part of it is
// negative; the window manager fits a cell that reaches past the grid.
static bool parse_grid(char *text, unsigned grid[6])
{
    int value[6];
    if (sscanf(text, ARGUMENT_WINDOW_GRID, &value[0], &value[1], &value[2], &value[3], &value[4], &value[5]) != 6) {
        return false;
    }

    if (value[0] < 1 || value[1] < 1) return false;
    for (int i = 2; i < 6; ++i) {
        if (value[i] < 0) return false;
    }

    for (int i = 0; i < 6; ++i) {
        grid[i] = (unsigned) value[i];
    }

    return true;
}

void daemon_fail(FILE *rsp, char *fmt, ...)
{
    if (!rsp) return;

    va_list ap;
    va_start(ap, fmt);
    fprintf(rsp, FAILURE_MESSAGE);
    vfprintf(rsp, fmt, ap);
    va_end(ap);
}

__unused __attribute__((format(printf, 2, 3))) static inline void daemon_deprecated(FILE *rsp, char *fmt, ...)
{
    if (!rsp) return;

    va_list ap;
    va_start(ap, fmt);
    fprintf(rsp, "deprecation warning: ");
    vfprintf(rsp, fmt, ap);
    va_end(ap);
}

static void parse_key_value_pair(char *token, char **key, char **value, bool *exclusion)
{
    *key = token;

    while (*token) {
        char fst = token[0];
        char snd = token[1];

        if (fst == '!' && snd == '=') {
            break;
        } else if (fst == '=') {
            break;
        }

        ++token;
    }

    int index = (token[0] == '!' && token[1] == '=') ? 2 : 1;
    char check = (index == 2) ? '!' : '=';

    if (*token != check) {
        *key = NULL;
        *value = NULL;
    } else if (token[index]) {
        *token = '\0';
        *value = token+index;
        *exclusion = index == 2;
    } else {
        *value = NULL;
    }
}

static uint8_t parse_value_type(char *type)
{
    if (string_equals(type, "abs")) {
        return TYPE_ABS;
    } else if (string_equals(type, "rel")) {
        return TYPE_REL;
    } else {
        return 0;
    }
}

static uint8_t parse_resize_handle(char *handle)
{
    if (string_equals(handle, "top")) {
        return HANDLE_TOP;
    } else if (string_equals(handle, "bottom")) {
        return HANDLE_BOTTOM;
    } else if (string_equals(handle, "left")) {
        return HANDLE_LEFT;
    } else if (string_equals(handle, "right")) {
        return HANDLE_RIGHT;
    } else if (string_equals(handle, "top_left")) {
        return HANDLE_TOP | HANDLE_LEFT;
    } else if (string_equals(handle, "top_right")) {
        return HANDLE_TOP | HANDLE_RIGHT;
    } else if (string_equals(handle, "bottom_left")) {
        return HANDLE_BOTTOM | HANDLE_LEFT;
    } else if (string_equals(handle, "bottom_right")) {
        return HANDLE_BOTTOM | HANDLE_RIGHT;
    } else if (string_equals(handle, "abs")) {
        return HANDLE_ABS;
    } else {
        return 0;
    }
}

static char *reserved_display_identifiers[] =
{
    ARGUMENT_COMMON_SEL_NORTH,
    ARGUMENT_COMMON_SEL_EAST,
    ARGUMENT_COMMON_SEL_SOUTH,
    ARGUMENT_COMMON_SEL_WEST,
    ARGUMENT_COMMON_SEL_PREV,
    ARGUMENT_COMMON_SEL_NEXT,
    ARGUMENT_COMMON_SEL_FIRST,
    ARGUMENT_COMMON_SEL_LAST,
    ARGUMENT_COMMON_SEL_RECENT,
    ARGUMENT_COMMON_SEL_MOUSE
};

static char *reserved_space_identifiers[] =
{
    ARGUMENT_COMMON_SEL_PREV,
    ARGUMENT_COMMON_SEL_NEXT,
    ARGUMENT_COMMON_SEL_FIRST,
    ARGUMENT_COMMON_SEL_LAST,
    ARGUMENT_COMMON_SEL_RECENT,
    ARGUMENT_COMMON_SEL_MOUSE
};

static char *reserved_window_identifiers[] =
{
    ARGUMENT_WINDOW_TOGGLE_FLOAT,
    ARGUMENT_WINDOW_TOGGLE_STICKY,
    ARGUMENT_WINDOW_TOGGLE_SHADOW,
    ARGUMENT_WINDOW_TOGGLE_SPLIT,
    ARGUMENT_WINDOW_TOGGLE_PARENT,
    ARGUMENT_WINDOW_TOGGLE_FULLSC,
    ARGUMENT_WINDOW_TOGGLE_WINDOWED,
    ARGUMENT_WINDOW_TOGGLE_NATIVE,
    ARGUMENT_WINDOW_TOGGLE_EXPOSE,
    ARGUMENT_WINDOW_TOGGLE_PIP,
    ARGUMENT_WINDOW_SCRATCHPAD_RECOVER
};

static bool parse_label(FILE *rsp, struct token token, enum label_type type, char **label)
{
    struct token_value value = token_to_value(token);

    if (value.type == TOKEN_TYPE_INVALID) {
        *label = NULL;
        return true;
    }

    if (value.type != TOKEN_TYPE_STRING) {
        daemon_fail(rsp, "'%.*s' cannot be used as a label.\n", token.length, token.text);
        return false;
    }

    switch (type) {
    default: break;
    case LABEL_DISPLAY: {
        for (int i = 0; i < array_count(reserved_display_identifiers); ++i) {
            if (token_equals(token, reserved_display_identifiers[i])) {
                daemon_fail(rsp, "'%.*s' is a reserved keyword and cannot be used as a label.\n", token.length, token.text);
                return false;
            }
        }
    } break;
    case LABEL_SPACE: {
        for (int i = 0; i < array_count(reserved_space_identifiers); ++i) {
            if (token_equals(token, reserved_space_identifiers[i])) {
                daemon_fail(rsp, "'%.*s' is a reserved keyword and cannot be used as a label.\n", token.length, token.text);
                return false;
            }
        }
    } break;
    case LABEL_WINDOW: {
        for (int i = 0; i < array_count(reserved_window_identifiers); ++i) {
            if (token_equals(token, reserved_window_identifiers[i])) {
                daemon_fail(rsp, "'%.*s' is a reserved keyword and cannot be used as a scratchpad.\n", token.length, token.text);
                return false;
            }
        }
    } break;
    }

    *label = malloc(token.length + 1);
    if (!(*label)) return false;

    memcpy(*label, token.text, token.length);
    (*label)[token.length] = '\0';

    return true;
}

static inline bool parse_property(struct properties *properties, char *property, uint64_t *property_val, char **property_str, int property_count)
{
    for (int i = 0; i < property_count; ++i) {
        if (string_equals(property, property_str[i])) {
            properties->flags |= property_val[i];
            return true;
        }
    }

    return false;
}

static struct properties parse_properties(FILE *rsp, struct token token, uint64_t *property_val, char **property_str, int property_count)
{
    struct properties result = { .token = token, .did_error = false };

    if ((result.did_parse = token_is_valid(token) && !token_prefix(token, "--"))) {
        for (int i = 0, cursor = 0; i < token.length; ++i) {
            if (i+1 == token.length) {
                if (!parse_property(&result, token.text+cursor, property_val, property_str, property_count)) {
                    daemon_fail(rsp, "'%.*s' is not a valid property.\n", i-cursor+1, token.text+cursor);
                    result.did_error = true;
                }
            } else if (token.text[i] == ',') {
                token.text[i] = '\0';

                if (!parse_property(&result, token.text+cursor, property_val, property_str, property_count)) {
                    daemon_fail(rsp, "'%.*s' is not a valid property.\n", i-cursor+1, token.text+cursor);
                    result.did_error = true;
                }

                cursor = i+1;
            }
        }
    }

    return result;
}

static struct selector parse_display_selector(FILE *rsp, char **message, uint32_t acting_did, bool optional)
{
    TIME_FUNCTION;

    struct selector result = { .token = get_token(message), .did_parse = true };

    struct token_value value = token_to_value(result.token);
    if (value.type == TOKEN_TYPE_INT) {
        uint32_t did = display_manager_arrangement_display_id(value.int_value);
        if (did) {
            result.did = did;
        } else {
            daemon_fail(rsp, "could not locate display with arrangement index '%d'.\n", value.int_value);
        }
    } else if (value.type == TOKEN_TYPE_STRING) {
        if (token_equals(result.token, ARGUMENT_COMMON_SEL_NORTH)) {
            if (acting_did) {
                uint32_t did = display_manager_find_closest_display_in_direction(acting_did, DIR_NORTH);
                if (did) {
                    result.did = did;
                } else {
                    daemon_fail(rsp, "could not locate a northward display.\n");
                }
            } else {
                daemon_fail(rsp, "could not locate the selected display.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_EAST)) {
            if (acting_did) {
                uint32_t did = display_manager_find_closest_display_in_direction(acting_did, DIR_EAST);
                if (did) {
                    result.did = did;
                } else {
                    daemon_fail(rsp, "could not locate a eastward display.\n");
                }
            } else {
                daemon_fail(rsp, "could not locate the selected display.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_SOUTH)) {
            if (acting_did) {
                uint32_t did = display_manager_find_closest_display_in_direction(acting_did, DIR_SOUTH);
                if (did) {
                    result.did = did;
                } else {
                    daemon_fail(rsp, "could not locate a southward display.\n");
                }
            } else {
                daemon_fail(rsp, "could not locate the selected display.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_WEST)) {
            if (acting_did) {
                uint32_t did = display_manager_find_closest_display_in_direction(acting_did, DIR_WEST);
                if (did) {
                    result.did = did;
                } else {
                    daemon_fail(rsp, "could not locate a westward display.\n");
                }
            } else {
                daemon_fail(rsp, "could not locate the selected display.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_PREV)) {
            if (acting_did) {
                uint32_t did = display_manager_prev_display_id(acting_did);
                if (did) {
                    result.did = did;
                } else {
                    daemon_fail(rsp, "could not locate the previous display.\n");
                }
            } else {
                daemon_fail(rsp, "could not locate the selected display.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_NEXT)) {
            if (acting_did) {
                uint32_t did = display_manager_next_display_id(acting_did);
                if (did) {
                    result.did = did;
                } else {
                    daemon_fail(rsp, "could not locate the next display.\n");
                }
            } else {
                daemon_fail(rsp, "could not locate the selected display.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_FIRST)) {
            uint32_t did = display_manager_first_display_id();
            if (did) {
                result.did = did;
            } else {
                daemon_fail(rsp, "could not locate the first display.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_LAST)) {
            uint32_t did = display_manager_last_display_id();
            if (did) {
                result.did = did;
            } else {
                daemon_fail(rsp, "could not locate the last display.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_RECENT)) {
            result.did = g_display_manager.last_display_id;
        } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_MOUSE)) {
            uint32_t did = display_manager_cursor_display_id();
            if (did) {
                result.did = did;
            } else {
                daemon_fail(rsp, "could not locate display containing cursor.\n");
            }
        } else {
            struct display_label *display_label = display_manager_get_display_for_label(&g_display_manager, value.string_value);
            if (display_label) {
                result.did_parse = true;
                result.did = display_label->did;
            } else {
                result.did_parse = false;
                daemon_fail(rsp, "value '%.*s' is not a valid option for DISPLAY_SEL\n", result.token.length, result.token.text);
            }
        }
    } else if (value.type == TOKEN_TYPE_INVALID) {
        result.did_parse = false;
        if (!optional) daemon_fail(rsp, "value '%.*s' is not a valid option for DISPLAY_SEL\n", result.token.length, result.token.text);
    } else {
        result.did_parse = false;
        daemon_fail(rsp, "value '%.*s' is not a valid option for DISPLAY_SEL\n", result.token.length, result.token.text);
    }

    return result;
}

struct selector parse_space_selector(FILE *rsp, char **message, uint64_t acting_sid, bool optional)
{
    TIME_FUNCTION;

    struct selector result = { .token = get_token(message), .did_parse = true };

    struct token_value value = token_to_value(result.token);
    if (value.type == TOKEN_TYPE_INT) {
        uint64_t sid = space_manager_mission_control_space(value.int_value);
        if (sid) {
            result.sid = sid;
        } else {
            daemon_fail(rsp, "could not locate space with mission-control index '%d'.\n", value.int_value);
        }
    } else if (value.type == TOKEN_TYPE_STRING) {
        if (token_equals(result.token, ARGUMENT_COMMON_SEL_PREV)) {
            if (acting_sid) {
                uint64_t sid = space_manager_prev_space(acting_sid);
                if (sid) {
                    result.sid = sid;
                } else {
                    daemon_fail(rsp, "could not locate the previous space.\n");
                }
            } else {
                daemon_fail(rsp, "could not locate the selected space.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_NEXT)) {
            if (acting_sid) {
                uint64_t sid = space_manager_next_space(acting_sid);
                if (sid) {
                    result.sid = sid;
                } else {
                    daemon_fail(rsp, "could not locate the next space.\n");
                }
            } else {
                daemon_fail(rsp, "could not locate the selected space.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_FIRST)) {
            uint64_t sid = space_manager_first_space();
            if (sid) {
                result.sid = sid;
            } else {
                daemon_fail(rsp, "could not locate the first space.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_LAST)) {
            uint64_t sid = space_manager_last_space();
            if (sid) {
                result.sid = sid;
            } else {
                daemon_fail(rsp, "could not locate the last space.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_RECENT)) {
            result.sid = g_space_manager.last_space_id;
        } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_MOUSE)) {
            uint64_t sid = space_manager_cursor_space();
            if (sid) {
                result.sid = sid;
            } else {
                daemon_fail(rsp, "could not locate space containing cursor.\n");
            }
        } else {
            struct space_label *space_label = space_manager_get_space_for_label(&g_space_manager, value.string_value);
            if (space_label) {
                result.did_parse = true;
                result.sid = space_label->sid;
            } else {
                result.did_parse = false;
                daemon_fail(rsp, "value '%.*s' is not a valid option for SPACE_SEL\n", result.token.length, result.token.text);
            }
        }
    } else if (value.type == TOKEN_TYPE_INVALID) {
        result.did_parse = false;
        if (!optional) daemon_fail(rsp, "value '%.*s' is not a valid option for SPACE_SEL\n", result.token.length, result.token.text);
    } else {
        result.did_parse = false;
        daemon_fail(rsp, "value '%.*s' is not a valid option for SPACE_SEL\n", result.token.length, result.token.text);
    }

    return result;
}

static struct selector parse_window_selector(FILE *rsp, char **message, struct window *acting_window, bool optional)
{
    TIME_FUNCTION;

    struct selector result = { .token = get_token(message), .did_parse = true };

    struct token_value value = token_to_value(result.token);
    if (value.type == TOKEN_TYPE_INT) {
        struct window *window = window_manager_find_window(&g_window_manager, value.int_value);
        if (window) {
            result.window = window;
        } else {
            daemon_fail(rsp, "could not locate window with the specified id '%d'.\n", value.int_value);
        }
    } else if (value.type == TOKEN_TYPE_STRING) {
        if (token_equals(result.token, ARGUMENT_COMMON_SEL_NORTH)) {
            if (acting_window) {
                struct window *closest_window = window_manager_find_closest_managed_window_in_direction(&g_window_manager, acting_window, DIR_NORTH);
                if (closest_window) {
                    result.window = closest_window;
                } else {
                    daemon_fail(rsp, "could not locate a northward managed window.\n");
                }
            } else {
                daemon_fail(rsp, "could not locate the selected window.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_EAST)) {
            if (acting_window) {
                struct window *closest_window = window_manager_find_closest_managed_window_in_direction(&g_window_manager, acting_window, DIR_EAST);
                if (closest_window) {
                    result.window = closest_window;
                } else {
                    daemon_fail(rsp, "could not locate a eastward managed window.\n");
                }
            } else {
                daemon_fail(rsp, "could not locate the selected window.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_SOUTH)) {
            if (acting_window) {
                struct window *closest_window = window_manager_find_closest_managed_window_in_direction(&g_window_manager, acting_window, DIR_SOUTH);
                if (closest_window) {
                    result.window = closest_window;
                } else {
                    daemon_fail(rsp, "could not locate a southward managed window.\n");
                }
            } else {
                daemon_fail(rsp, "could not locate the selected window.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_WEST)) {
            if (acting_window) {
                struct window *closest_window = window_manager_find_closest_managed_window_in_direction(&g_window_manager, acting_window, DIR_WEST);
                if (closest_window) {
                    result.window = closest_window;
                } else {
                    daemon_fail(rsp, "could not locate a westward managed window.\n");
                }
            } else {
                daemon_fail(rsp, "could not locate the selected window.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_MOUSE)) {
            struct window *mouse_window = window_manager_find_window_below_cursor(&g_window_manager);
            if (mouse_window) {
                result.window = mouse_window;
            } else {
                daemon_fail(rsp, "could not locate a window below the cursor.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_WINDOW_SEL_LARGEST)) {
            struct window *area_window = window_manager_find_largest_managed_window(&g_space_manager, &g_window_manager);
            if (area_window) {
                result.window = area_window;
            } else {
                daemon_fail(rsp, "could not locate window with the largest area.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_WINDOW_SEL_SMALLEST)) {
            struct window *area_window = window_manager_find_smallest_managed_window(&g_space_manager, &g_window_manager);
            if (area_window) {
                result.window = area_window;
            } else {
                daemon_fail(rsp, "could not locate window with the smallest area.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_WINDOW_SEL_SIBLING)) {
            if (acting_window) {
                struct window *sibling_window = window_manager_find_sibling_for_managed_window(&g_window_manager, acting_window);
                if (sibling_window) {
                    result.window = sibling_window;
                } else {
                    daemon_fail(rsp, "could not locate sibling of window.\n");
                }
            } else {
                daemon_fail(rsp, "could not locate the selected window.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_WINDOW_SEL_FNEPHEW)) {
            if (acting_window) {
                struct window *nephew_window = window_manager_find_first_nephew_for_managed_window(&g_window_manager, acting_window);
                if (nephew_window) {
                    result.window = nephew_window;
                } else {
                    daemon_fail(rsp, "could not locate first nephew of window.\n");
                }
            } else {
                daemon_fail(rsp, "could not locate the selected window.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_WINDOW_SEL_SNEPHEW)) {
            if (acting_window) {
                struct window *nephew_window = window_manager_find_second_nephew_for_managed_window(&g_window_manager, acting_window);
                if (nephew_window) {
                    result.window = nephew_window;
                } else {
                    daemon_fail(rsp, "could not locate second nephew of window.\n");
                }
            } else {
                daemon_fail(rsp, "could not locate the selected window.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_WINDOW_SEL_UNCLE)) {
            if (acting_window) {
                struct window *uncle_window = window_manager_find_uncle_for_managed_window(&g_window_manager, acting_window);
                if (uncle_window) {
                    result.window = uncle_window;
                } else {
                    daemon_fail(rsp, "could not locate uncle of window.\n");
                }
            } else {
                daemon_fail(rsp, "could not locate the selected window.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_WINDOW_SEL_FCOUSIN)) {
            if (acting_window) {
                struct window *cousin_window = window_manager_find_first_cousin_for_managed_window(&g_window_manager, acting_window);
                if (cousin_window) {
                    result.window = cousin_window;
                } else {
                    daemon_fail(rsp, "could not locate first cousin of window.\n");
                }
            } else {
                daemon_fail(rsp, "could not locate the selected window.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_WINDOW_SEL_SCOUSIN)) {
            if (acting_window) {
                struct window *cousin_window = window_manager_find_second_cousin_for_managed_window(&g_window_manager, acting_window);
                if (cousin_window) {
                    result.window = cousin_window;
                } else {
                    daemon_fail(rsp, "could not locate second cousin of window.\n");
                }
            } else {
                daemon_fail(rsp, "could not locate the selected window.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_PREV)) {
            if (acting_window) {
                struct window *prev_window = window_manager_find_prev_managed_window(&g_space_manager, &g_window_manager, acting_window);
                if (prev_window) {
                    result.window = prev_window;
                } else {
                    daemon_fail(rsp, "could not locate the prev managed window.\n");
                }
            } else {
                daemon_fail(rsp, "could not locate the selected window.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_NEXT)) {
            if (acting_window) {
                struct window *next_window = window_manager_find_next_managed_window(&g_space_manager, &g_window_manager, acting_window);
                if (next_window) {
                    result.window = next_window;
                } else {
                    daemon_fail(rsp, "could not locate the next managed window.\n");
                }
            } else {
                daemon_fail(rsp, "could not locate the selected window.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_FIRST)) {
            struct window *first_window = window_manager_find_first_managed_window(&g_space_manager, &g_window_manager);
            if (first_window) {
                result.window = first_window;
            } else {
                daemon_fail(rsp, "could not locate the first managed window.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_LAST)) {
            struct window *last_window = window_manager_find_last_managed_window(&g_space_manager, &g_window_manager);
            if (last_window) {
                result.window = last_window;
            } else {
                daemon_fail(rsp, "could not locate the last managed window.\n");
            }
        } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_RECENT)) {
            struct window *recent_window = window_manager_find_recent_managed_window(&g_window_manager);
            if (recent_window) {
                result.window = recent_window;
            } else {
                daemon_fail(rsp, "could not locate the most recently focused window.\n");
            }
        } else if (token_prefix(result.token, ARGUMENT_COMMON_SEL_STACK_PREFIX)) {
            if (acting_window) {
                int index;
                result.token.text   += strlen(ARGUMENT_COMMON_SEL_STACK_PREFIX);
                result.token.length -= strlen(ARGUMENT_COMMON_SEL_STACK_PREFIX);

                if (token_equals(result.token, ARGUMENT_COMMON_SEL_PREV)) {
                    struct window *prev_window = window_manager_find_prev_window_in_stack(&g_space_manager, &g_window_manager, acting_window);
                    if (prev_window) {
                        result.window = prev_window;
                    } else {
                        daemon_fail(rsp, "could not locate the prev stacked window.\n");
                    }
                } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_NEXT)) {
                    struct window *next_window = window_manager_find_next_window_in_stack(&g_space_manager, &g_window_manager, acting_window);
                    if (next_window) {
                        result.window = next_window;
                    } else {
                        daemon_fail(rsp, "could not locate the next stacked window.\n");
                    }
                } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_FIRST)) {
                    struct window *first_window = window_manager_find_first_window_in_stack(&g_space_manager, &g_window_manager, acting_window);
                    if (first_window) {
                        result.window = first_window;
                    } else {
                        daemon_fail(rsp, "could not locate the first stacked window.\n");
                    }
                } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_LAST)) {
                    struct window *last_window = window_manager_find_last_window_in_stack(&g_space_manager, &g_window_manager, acting_window);
                    if (last_window) {
                        result.window = last_window;
                    } else {
                        daemon_fail(rsp, "could not locate the last stacked window.\n");
                    }
                } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_RECENT)) {
                    struct window *recent_window = window_manager_find_recent_window_in_stack(&g_space_manager, &g_window_manager, acting_window);
                    if (recent_window) {
                        result.window = recent_window;
                    } else {
                        daemon_fail(rsp, "could not locate the recent stacked window.\n");
                    }
                } else if (token_is_valid(result.token) && token_is_positive_integer(result.token, &index) && index > 0) {
                    struct window *index_window = window_manager_find_window_in_stack(&g_space_manager, &g_window_manager, acting_window, index);
                    if (index_window) {
                        result.window = index_window;
                    } else {
                        daemon_fail(rsp, "could not locate the stacked window in position %d.\n", index);
                    }
                } else {
                    result.did_parse = false;
                    daemon_fail(rsp, "value '%s%.*s' is not a valid option for WINDOW_SEL\n", ARGUMENT_COMMON_SEL_STACK_PREFIX, result.token.length, result.token.text);
                }
            } else {
                daemon_fail(rsp, "could not locate the selected window.\n");
            }
        } else {
            result.did_parse = false;
            daemon_fail(rsp, "value '%.*s' is not a valid option for WINDOW_SEL\n", result.token.length, result.token.text);
        }
    } else if (value.type == TOKEN_TYPE_INVALID) {
        result.did_parse = false;
        if (!optional) daemon_fail(rsp, "value '%.*s' is not a valid option for WINDOW_SEL\n", result.token.length, result.token.text);
    } else {
        result.did_parse = false;
        daemon_fail(rsp, "value '%.*s' is not a valid option for WINDOW_SEL\n", result.token.length, result.token.text);
    }

    return result;
}

static struct selector parse_insert_selector(FILE *rsp, char **message)
{
    struct selector result = { .token = get_token(message), .did_parse = true };

    if (token_equals(result.token, ARGUMENT_COMMON_SEL_NORTH)) {
        result.dir = DIR_NORTH;
    } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_EAST)) {
        result.dir = DIR_EAST;
    } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_SOUTH)) {
        result.dir = DIR_SOUTH;
    } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_WEST)) {
        result.dir = DIR_WEST;
    } else if (token_equals(result.token, ARGUMENT_COMMON_SEL_STACK)) {
        result.dir = STACK;
    } else {
        result.did_parse = false;
        daemon_fail(rsp, "value '%.*s' is not a valid option for DIR_SEL\n", result.token.length, result.token.text);
    }

    return result;
}
