#include "space_navigation.c"

static uint64_t space_navigation_active_space(void)
{
    // Read WindowServer's active display directly; querying the focused
    // application's AX window during every switch can stall navigation.
    return display_space_id(display_manager_active_display_id());
}

// The space `steps` places after sid in mission-control order, wrapping.
static uint64_t space_navigation_step(uint64_t sid, int steps)
{
    CFArrayRef display_spaces_ref = SLSCopyManagedDisplaySpaces(g_connection);
    if (!display_spaces_ref) return 0;

    uint64_t *space_list = NULL;
    int index = 0;

    int display_spaces_count = CFArrayGetCount(display_spaces_ref);
    for (int i = 0; i < display_spaces_count; ++i) {
        CFDictionaryRef display_ref = CFArrayGetValueAtIndex(display_spaces_ref, i);
        CFArrayRef spaces_ref = CFDictionaryGetValue(display_ref, CFSTR("Spaces"));

        int spaces_count = CFArrayGetCount(spaces_ref);
        for (int j = 0; j < spaces_count; ++j) {
            CFDictionaryRef space_ref = CFArrayGetValueAtIndex(spaces_ref, j);
            CFNumberRef sid_ref = CFDictionaryGetValue(space_ref, CFSTR("id64"));

            uint64_t space_id = 0;
            CFNumberGetValue(sid_ref, CFNumberGetType(sid_ref), &space_id);

            ts_buf_push(space_list, space_id);
            if (space_id == sid) index = ts_buf_len(space_list);
        }
    }

    CFRelease(display_spaces_ref);

    if (!index) return 0;

    int count = ts_buf_len(space_list);
    return space_list[space_navigation_step_index(index, count, steps) - 1];
}

// Accept thread. The client sends its whole request right after connecting;
// a request that is not readable almost at once is posted as usual.
static bool space_navigation_accept(int sockfd)
{
    char bytes[128];
    int direction = 0;
    struct pollfd readable = { .fd = sockfd, .events = POLLIN };

    if (poll(&readable, 1, 10) == 1) {
        ssize_t length = recv(sockfd, bytes, sizeof(bytes), MSG_PEEK);
        if (length > 0) direction = space_navigation_request_direction(bytes, (int) length);
    }

    if (!space_navigation_queue_join(sockfd, direction, read_os_timer())) return false;

    while (recv(sockfd, bytes, sizeof(bytes), MSG_DONTWAIT) > 0);
    socket_close(sockfd);

    return true;
}

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

    uint64_t current = space_navigation_active_space();
    struct selector selector = parse_space_selector(NULL, message, current, false);

    if (!selector.sid && token_equals(selector.token, ARGUMENT_COMMON_SEL_NEXT)) {
        selector.sid = space_manager_first_space();
    }

    if (!selector.sid && token_equals(selector.token, ARGUMENT_COMMON_SEL_PREV)) {
        selector.sid = space_manager_last_space();
    }

    // Requests that joined this one while it waited move it further.
    if (!move && g_space_navigation_claim.active && current) {
        selector.sid = g_space_navigation_claim.steps
                     ? space_navigation_step(current, g_space_navigation_claim.steps)
                     : current;
    }

    float alpha;
    float duration;
    struct token from = get_token(message);
    struct token time = get_token(message);

    if (!current || !selector.did_parse || !selector.sid
        || !space_navigation_number(from, &alpha) || alpha == 0.0f
        || !space_navigation_number(time, &duration)) {
        daemon_fail(rsp, "navigate expects SPACE_SEL, opacity in (0,1] and duration in [0,1] seconds.\n");
        return;
    }

    if (!space_navigation_run(current, selector.sid, move, alpha, duration)) {
        daemon_fail(rsp, "navigation failed: check scripting addition, Mission Control, display animation and window eligibility.\n");
    }
}
