// Connect the real schedule and asynchronous step. Only host services are
// mocked: no capture, Dock request or focus reaches the running desktop.
#define debug(...)
#define NAVIGATION_REAL_SCHEDULE
#include "fixture.h"

static uint64_t wake_due;
static void space_navigation_schedule_after(uint64_t delay_ns)
{
    wake_due = timestamp + delay_ns;
}

static enum space_navigation_result space_navigation_execute(struct space_navigation_request *request, int steps,
                                                             bool activate, bool settle, float duration)
{
    uint64_t from = request->sid ? request->sid : active_space;
    // Deterministic six-Desktop topology; production plan/switch/finish are real.
    uint64_t sid = steps ? (uint64_t)(((int)from - 1 + steps + 600) % 6 + 1) : from;
    struct space_navigation_step step = {
        .sid = sid, .move = request->move, .crossfade = request->crossfade,
        .alpha = request->alpha, .duration = duration, .activate = activate, .settle = settle
    };
    return space_navigation_begin_step(active_space, &step);
}

static void press(int steps, bool fast)
{
    struct space_navigation_request request = {
        .steps = steps, .fast = fast, .crossfade = true, .alpha = 1.0f, .duration = .25f
    };
    assert(space_navigation_schedule_add(&request, false));
    space_navigation_schedule_pump();
}

static void test_fast_pending(void)
{
    reset();
    press(1, false);
    assert(space_navigation_flight.active && g_space_navigation_schedule.running);
    int old_token = capture_token;
    timestamp += 30000000ULL;
    press(1, true);
    // A new quick request completes the already-planned switch now, rather
    // than waiting for its callback or presentation deadline.
    assert(active_space == 2 && focus_calls == 1);
    assert(!space_navigation_flight.active && !g_space_navigation_schedule.running);
    assert(present_calls == 0 && snapshot_starts == 0 && window_focus_calls == 0);
    assert(g_space_navigation_schedule.count == 1);
    assert(g_space_navigation_schedule.last_step == timestamp);
    assert(wake_due == timestamp + SPACE_NAVIGATION_RHYTHM_NS);
    space_navigation_step_captured(old_token);
    space_navigation_step_captured(old_token); // Callback and deadline cannot complete twice.
    assert(focus_calls == 1 && present_calls == 0);
    timestamp += SPACE_NAVIGATION_RHYTHM_NS - 1;
    space_navigation_schedule_timer();
    assert(active_space == 2 && focus_calls == 1);
    ++timestamp;
    space_navigation_schedule_timer();
    assert(active_space == 3 && focus_calls == 2 && window_focus_calls == 1 && focused_id == 2);
    assert(g_space_navigation_schedule.count == 0 && !g_space_navigation_schedule.running);
}

static void tick(uint64_t delay)
{
    timestamp += delay;
    space_navigation_schedule_timer();
}

static void number(uint64_t sid, bool fast, bool move)
{
    struct space_navigation_request request = {
        .sid = sid, .fast = fast, .move = move, .crossfade = true, .alpha = 1.0f, .duration = .25f
    };
    assert(space_navigation_schedule_add(&request, false));
    space_navigation_schedule_pump();
}

static void drain_fast(void)
{
    int budget = 8;
    while (g_space_navigation_schedule.count && --budget) {
        tick(SPACE_NAVIGATION_RHYTHM_NS);
    }
    assert(budget && !g_space_navigation_schedule.count && !g_space_navigation_schedule.running);
}

static void test_order_selectors_and_bound(void)
{
    reset();
    press(1, false);
    timestamp += 30000000;
    press(-1, true);
    assert(active_space == 2 && window_focus_calls == 0);
    drain_fast();
    assert(active_space == 1 && focus_calls == 2 && focused_id == 2);

    // Opposite pending inputs cancel each other; the running switch still
    // completes and focuses its own destination once.
    reset();
    press(1, false);
    press(1, false);
    press(-1, true);
    assert(active_space == 2 && g_space_navigation_schedule.count == 0);
    assert(focused_id == 1 && window_focus_calls == 1 && !g_space_navigation_schedule.unsettled);
    space_navigation_step_skip_effect();
    assert(focus_calls == 1 && discard_calls == 1); // Idempotent.

    // The running intermediate step had already deferred activation. If the
    // queued remainder cancels, quiet settling must focus the reached Desktop.
    reset();
    press(2, false);
    timestamp += 30000000;
    press(-1, true);
    assert(active_space == 2 && window_focus_calls == 0);
    assert(g_space_navigation_schedule.unsettled && !g_space_navigation_schedule.count);
    tick(SPACE_NAVIGATION_SETTLE_NS - 1);
    assert(window_focus_calls == 0);
    tick(1);
    assert(active_space == 2 && focused_id == 1 && window_focus_calls == 1);
    assert(focus_calls == 1 && !g_space_navigation_schedule.unsettled);

    // A held key keeps only one step pending in its direction, including
    // repeats arriving while the first image is pending.
    reset();
    press(1, false);
    timestamp += 30000000;
    struct space_navigation_request repeat = {
        .steps = 1, .fast = true, .crossfade = true, .alpha = 1.0f, .duration = .25f
    };
    for (int i = 0; i < 12; ++i) {
        assert(space_navigation_schedule_add(&repeat, true));
        space_navigation_schedule_pump();
    }
    assert(space_navigation_schedule_steps() == 1 && discard_calls == 1);
    drain_fast();
    assert(active_space == 3 && focused_id == 2 && window_focus_calls == 1);

    reset();
    press(1, false);
    timestamp += 30000000;
    number(5, true, false);
    press(1, true);
    drain_fast();
    assert(active_space == 6 && focus_calls == 3 && window_focus_calls == 1 && focused_id == 1);

    // A full queue retains all relative input in its bounded final jump.
    reset();
    press(1, false);
    timestamp += 30000000;
    for (int i = 0; i < 14; ++i) press(1, true);
    assert(space_navigation_schedule_steps() == SPACE_NAVIGATION_QUEUE_STEPS);
    drain_fast();
    assert(active_space == 4 && focus_calls == 5 && window_focus_calls == 1 && focused_id == 1);
}

static void test_new_flight_and_cancellation(void)
{
    reset();
    press(1, false);
    int old_token = capture_token;
    timestamp += 30000000;
    press(1, true);
    drain_fast();
    space_navigation_schedule_focused(2);
    tick(SPACE_NAVIGATION_RHYTHM_NS);
    number(4, false, false);
    int new_token = capture_token;
    assert(new_token != old_token && space_navigation_flight.active && pending_token == new_token);
    space_navigation_step_captured(old_token);
    assert(space_navigation_flight.active && pending_token == new_token && active_space == 3);
    space_navigation_step_captured(new_token);
    assert(active_space == 4 && focused_id == 1 && window_focus_calls == 2 && present_calls == 1);
    int switches = focus_calls;
    space_navigation_step_captured(old_token);
    space_navigation_step_captured(new_token);
    assert(focus_calls == switches && !space_navigation_flight.active);

    // An unrelated command cancels flight+presentation+schedule together.
    // Its old token cannot change a later request's destination or focus.
    reset();
    press(1, false);
    old_token = capture_token;
    space_navigation_step_cancel();
    space_navigation_snapshot_cancel();
    space_navigation_schedule_cancel();
    press(-1, true);
    tick(SPACE_NAVIGATION_RHYTHM_NS);
    assert(active_space == 6 && focused_id == 1 && window_focus_calls == 1);
    space_navigation_step_captured(old_token);
    assert(active_space == 6 && focus_calls == 1 && present_calls == 0);

    // Known pointer/MC/animation state avoids presentation entirely and
    // preserves the existing logical cancellation policy.
    for (int kind = 0; kind < 3; ++kind) {
        reset();
        press(1, false);
        timestamp += 30000000;
        if (kind == 0) seconds_since_click = .001;
        if (kind == 1) mission_control = true;
        if (kind == 2) animating = true;
        press(1, true);
        assert(active_space == 1 && focus_calls == 0 && window_focus_calls == 0);
        assert(present_calls == 0 && discard_calls == 1 && g_space_navigation_schedule.count == 0);
        space_navigation_step_captured(capture_token);
        assert(focus_calls == 0);
    }
}

static void test_move_and_rejected_input(void)
{
    reset();
    number(2, false, true);
    assert(move_calls == 1 && move_sid == 2 && space_navigation_flight.active);
    timestamp += 30000000;
    press(1, true);
    assert(space_navigation_flight.active && discard_calls == 0 && focus_calls == 0);
    space_navigation_step_captured(capture_token);
    assert(active_space == 2 && click_raise_calls == 1 && focused_id == 1 && snapshot_starts == 1);
    space_navigation_schedule_focused(1);
    drain_fast();
    assert(active_space == 3 && window_focus_calls == 2 && focused_id == 2 && move_calls == 1);

    // A non-fast move arriving behind a focus flight keeps both operations.
    reset();
    press(1, false);
    timestamp += 30000000;
    number(4, false, true);
    assert(space_navigation_flight.active && discard_calls == 0);
    space_navigation_step_captured(capture_token);
    assert(active_space == 2 && window_focus_calls == 0);
    tick(300000000ULL); // Wait for the preceding effect guard.
    assert(move_calls == 1 && move_sid == 4 && space_navigation_flight.active);
    space_navigation_step_captured(capture_token);
    assert(active_space == 4 && click_raise_calls == 1 && focused_id == 1);

    // A rejected request does not abandon the current effect or complete it.
    reset();
    press(1, false);
    for (uint64_t sid = 3; sid <= 6; ++sid) number(sid, false, true);
    struct space_navigation_request rejected = {
        .steps = 1, .fast = true, .crossfade = true, .alpha = .5f, .duration = .25f
    };
    assert(!space_navigation_schedule_add(&rejected, false));
    assert(space_navigation_flight.active && g_space_navigation_schedule.running);
    assert(discard_calls == 0 && focus_calls == 0 && g_space_navigation_schedule.count == 4);
}

int main(void)
{
    test_fast_pending();
    test_order_selectors_and_bound();
    test_new_flight_and_cancellation();
    test_move_and_rejected_input();
    puts("navigation captured schedule: early completion, order, focus, cancellation and bounds passed");
    return 0;
}
