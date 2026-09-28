#include "../effects/display.m"
#include "topology.c"
#include "topology_other_displays.c"
#include "activation.c"
#include "step.c"
#include "schedule.c"

static void space_navigation_focus_schedule(int generation, uint64_t delay_ns)
{
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delay_ns), dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
        event_loop_post(&g_event_loop, SPACE_NAVIGATION_FOCUS, NULL, generation);
    });
}

static void space_navigation_schedule_after(uint64_t delay_ns)
{
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, delay_ns), dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
        event_loop_post(&g_event_loop, SPACE_NAVIGATION_DISPATCH, NULL, 0);
    });
}

static uint64_t space_navigation_active_space(void)
{
    // Read WindowServer's active display directly; querying the focused
    // application's AX window during every switch can stall navigation.
    return space_navigation_current_space(space_navigation_display_space(display_manager_active_display_id()));
}

// The space `steps` places after sid in mission-control order, wrapping.
static uint64_t space_navigation_step(uint64_t sid, int steps)
{
    int index = space_navigation_spaces_index(sid);
    if (!index) return 0;

    int count = g_space_navigation_spaces.count;
    return space_navigation_spaces_at(space_navigation_step_index(index, count, steps));
}

// A command that can change focus ends the anchor: relative navigation then
// starts from where that command left the user. It also wins over a deferred
// navigation focus and over the navigation still queued.
static void space_navigation_note_message(char *message)
{
    struct token domain = get_token(&message);
    if (token_equals(domain, DOMAIN_QUERY)) return;
    if (token_equals(domain, DOMAIN_SPACE) && token_equals(get_token(&message), COMMAND_SPACE_NAVIGATE)) return;

    space_navigation_snapshot_cancel();
    space_navigation_forget();
    space_navigation_focus_cancel();
    space_navigation_schedule_cancel();
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

// Runs one switch of a queued request: `steps` Desktops from where navigation
// stands, one except for a jump, or the request's own Desktop when 0. A jump
// from a Desktop number counts from that Desktop. A request handled at once
// reuses its snapshot of the Desktops; a later one takes its own.
static bool space_navigation_execute(struct space_navigation_request *request, int steps,
                                     bool activate, bool settle, float duration)
{
    bool loaded = g_space_navigation_spaces.loaded;

    if (!loaded) {
        CFArrayRef displays = SLSCopyManagedDisplaySpaces(g_connection);
        space_navigation_spaces_load(displays);
        if (displays) CFRelease(displays);
    }

    uint64_t current = space_navigation_active_space();
    uint64_t from = request->sid ? request->sid : current;
    uint64_t sid = steps ? space_navigation_step(from, steps) : request->sid;

    struct space_navigation_step step = {
        .sid = sid,
        .move = request->move,
        .crossfade = request->crossfade,
        .alpha = request->alpha,
        .duration = duration,
        .activate = activate,
        .settle = settle
    };

    bool success = current && sid && space_navigation_run_step(current, &step);

    if (!loaded) g_space_navigation_spaces.loaded = false;
    return success;
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

// Relative requests that joined this one and mission-control indices resolve
// from the snapshot; other selectors go through the ordinary parser.
static struct selector space_navigation_selector(char **message, uint64_t current, bool move)
{
    char *start = *message;
    struct token token = get_token(message);
    struct token_value value = token_to_value(token);

    // Requests that joined this one while it waited move it further.
    if (!move && g_space_navigation_claim.active && current) {
        uint64_t sid = g_space_navigation_claim.steps
                     ? space_navigation_step(current, g_space_navigation_claim.steps)
                     : current;

        return (struct selector) { .token = token, .did_parse = true, .sid = sid };
    }

    if (value.type == TOKEN_TYPE_INT) {
        return (struct selector) { .token = token, .did_parse = true, .sid = space_navigation_spaces_at(value.int_value) };
    }

    *message = start;
    struct selector selector = parse_space_selector(NULL, message, current, false);

    if (!selector.sid && token_equals(selector.token, ARGUMENT_COMMON_SEL_NEXT)) {
        selector.sid = space_manager_first_space();
    }

    if (!selector.sid && token_equals(selector.token, ARGUMENT_COMMON_SEL_PREV)) {
        selector.sid = space_manager_last_space();
    }

    return selector;
}

// The Desktops a relative request moves: the steps of the requests that
// joined it, or one for a plain next or prev. 0 for any other selector.
static int space_navigation_relative_steps(char *message, bool move, bool *repeat)
{
    *repeat = false;

    if (!move && g_space_navigation_claim.active) {
        *repeat = g_space_navigation_claim.repeat && abs(g_space_navigation_claim.steps) == 1;
        return g_space_navigation_claim.steps;
    }

    struct token token = get_token(&message);
    if (token_equals(token, ARGUMENT_COMMON_SEL_NEXT)) return 1;
    if (token_equals(token, ARGUMENT_COMMON_SEL_PREV)) return -1;

    return 0;
}

static void space_navigation_request(FILE *rsp, char **message)
{
    struct token action = get_token(message);
    bool move = token_equals(action, "move");

    if (!move && !token_equals(action, "focus")) {
        daemon_fail(rsp, "navigate expects focus or move.\n");
        return;
    }

    bool repeat;
    bool joined = !move && g_space_navigation_claim.active;
    int steps = space_navigation_relative_steps(*message, move, &repeat);

    uint64_t current = space_navigation_active_space();
    struct selector selector = space_navigation_selector(message, current, move);

    float alpha = 1.0f;
    float duration;
    struct token from = get_token(message);
    struct token time = get_token(message);
    bool crossfade = token_equals(from, "crossfade");

    if (!current || !selector.did_parse || !selector.sid
        || (!crossfade && (!space_navigation_number(from, &alpha) || alpha == 0.0f))
        || !space_navigation_number(time, &duration)) {
        daemon_fail(rsp, "navigate expects SPACE_SEL, crossfade or an opacity in (0,1], and a duration in [0,1] seconds.\n");
        return;
    }

    if (!space_navigation_schedule_pacing()) {
        struct space_navigation_step step = {
            .sid = selector.sid,
            .move = move,
            .crossfade = crossfade,
            .alpha = alpha,
            .duration = duration,
            .activate = true
        };

        if (!space_navigation_run_step(current, &step)) {
            daemon_fail(rsp, "navigation failed: check scripting addition, Mission Control, display animation and window eligibility.\n");
        }

        return;
    }

    // Joined requests whose steps cancelled out leave nothing to do.
    if (joined && !steps) return;

    struct space_navigation_request request = {
        .move = move,
        .steps = steps,
        .sid = steps ? 0 : selector.sid,
        .crossfade = crossfade,
        .alpha = alpha,
        .duration = duration
    };

    if (!space_navigation_schedule_add(&request, repeat)) {
        daemon_fail(rsp, "navigation queue is full.\n");
        return;
    }

    space_navigation_schedule_pump();
}

static void space_navigation_command(FILE *rsp, char **message)
{
    CFArrayRef displays = SLSCopyManagedDisplaySpaces(g_connection);
    space_navigation_spaces_load(displays);
    if (displays) CFRelease(displays);

    space_navigation_request(rsp, message);

    g_space_navigation_spaces.loaded = false;
}
