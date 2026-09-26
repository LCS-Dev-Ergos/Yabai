#include "space_navigation.c"

static bool space_navigation_number(struct token token, float *number)
{
    struct token_value value = token_to_value(token);

    if (value.type == TOKEN_TYPE_FLOAT) {
        *number = value.float_value;
    } else if (value.type == TOKEN_TYPE_INT) {
        *number = (float) value.int_value;
    } else {
        return false;
    }

    return isfinite(*number) && *number >= 0.0f && *number <= 1.0f;
}

static void space_navigation_command(FILE *rsp, char **message)
{
    struct token action = get_token(message);
    bool move = token_equals(action, "move");

    if (!move && !token_equals(action, "focus")) {
        daemon_fail(rsp, "navigate expects focus or move.\n");
        return;
    }

    uint64_t current = space_manager_active_space();
    struct selector selector = parse_space_selector(NULL, message, current, false);

    if (!selector.sid && token_equals(selector.token, ARGUMENT_COMMON_SEL_NEXT)) {
        selector.sid = space_manager_first_space();
    }

    if (!selector.sid && token_equals(selector.token, ARGUMENT_COMMON_SEL_PREV)) {
        selector.sid = space_manager_last_space();
    }

    float alpha;
    float duration;
    struct token from = get_token(message);
    struct token time = get_token(message);

    if (!selector.did_parse || !selector.sid
        || !space_navigation_number(from, &alpha) || alpha == 0.0f
        || !space_navigation_number(time, &duration)) {
        daemon_fail(rsp, "navigate expects SPACE_SEL, opacity in (0,1] and duration in [0,1] seconds.\n");
        return;
    }

    if (!space_navigation_run(selector.sid, move, alpha, duration)) {
        daemon_fail(rsp, "navigation failed: check scripting addition, Mission Control, display animation and window eligibility.\n");
    }
}
