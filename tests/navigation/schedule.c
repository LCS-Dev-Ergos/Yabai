#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MS 1000000ULL

static uint64_t now;
static double click_seconds;

static uint64_t read_os_timer(void)
{
    return now;
}

static double space_navigation_seconds_since_click(void)
{
    return click_seconds;
}

#define debug(...)

#include "../../src/space_navigation_schedule.c"

static struct
{
    int steps;
    uint64_t sid;
    bool activate;
    float duration;
    uint64_t time;
} executed[64];

static int executed_count;
static bool execute_success;
static uint32_t activate_window;
static uint64_t execute_delay;

// The window each activating step activates, if any, as navigation reports it.
static bool space_navigation_execute(struct space_navigation_request *request, int steps,
                                     bool activate, float duration)
{
    assert(executed_count < 64);
    executed[executed_count++] = (typeof(executed[0])) { steps, request->sid, activate, duration, now };
    now += execute_delay;

    if (execute_success) space_navigation_schedule_switched(request->crossfade ? duration : 0.0f);

    if (activate && activate_window) space_navigation_schedule_activated(activate_window);
    return execute_success;
}

static uint64_t wakes[64];
static int wake_count;

static void space_navigation_schedule_after(uint64_t delay_ns)
{
    assert(wake_count < 64);
    wakes[wake_count++] = now + delay_ns;
}

// Moves the clock, firing every requested wake that falls due on the way.
static void advance(uint64_t delay)
{
    uint64_t until = now + delay;

    for (;;) {
        int next = -1;
        for (int i = 0; i < wake_count; ++i) {
            if (wakes[i] && wakes[i] <= until && (next < 0 || wakes[i] < wakes[next])) next = i;
        }

        if (next < 0) break;

        now = wakes[next];
        wakes[next] = 0;
        space_navigation_schedule_timer();
    }

    now = until;
}

static int pending_wakes(void)
{
    int count = 0;
    for (int i = 0; i < wake_count; ++i) count += wakes[i] != 0;
    return count;
}

static void reset(void)
{
    // Long after anything earlier, with no click since.
    now += 10000 * MS;
    advance(0);

    memset(&g_space_navigation_schedule, 0, sizeof(g_space_navigation_schedule));
    g_space_navigation_schedule.pacing = true;
    memset(wakes, 0, sizeof(wakes));
    wake_count = 0;
    executed_count = 0;
    execute_success = true;
    activate_window = 0;
    execute_delay = 0;
    click_seconds = 1000.0;
}

static struct space_navigation_request relative(int steps, bool crossfade)
{
    return (struct space_navigation_request) {
        .steps = steps,
        .crossfade = crossfade,
        .alpha = crossfade ? 1.0f : 0.7f,
        .duration = 0.25f
    };
}

static struct space_navigation_request number(uint64_t sid, bool move)
{
    return (struct space_navigation_request) {
        .move = move,
        .sid = sid,
        .crossfade = true,
        .alpha = 1.0f,
        .duration = 0.25f
    };
}

static void press(int steps, bool repeat, bool crossfade)
{
    struct space_navigation_request request = relative(steps, crossfade);

    assert(space_navigation_schedule_add(&request, repeat));
    space_navigation_schedule_pump();
}

static void go_to(uint64_t sid)
{
    struct space_navigation_request request = number(sid, false);

    assert(space_navigation_schedule_add(&request, false));
    space_navigation_schedule_pump();
}

static int executed_steps(void)
{
    int steps = 0;

    for (int i = 0; i < executed_count; ++i) {
        steps += executed[i].steps;
    }

    return steps;
}

static void test_isolated_and_burst(void)
{
    // An isolated press runs at once, activates, and crossfades for as long
    // as it asked.
    reset();
    uint64_t start = now;
    activate_window = 7;
    press(1, false, true);
    assert(executed_count == 1 && executed[0].steps == 1 && executed[0].activate);
    assert(executed[0].time == start && executed[0].duration == 0.25f);

    // Presses that arrive meanwhile wait for the running blend, never
    // shortening or stacking it. The steps queued behind others blend within
    // the burst's pace; the last one keeps the requested duration.
    advance(30 * MS);
    press(1, false, true);
    advance(30 * MS);
    press(1, false, true);
    advance(30 * MS);
    press(1, false, true);
    assert(executed_count == 1);

    advance(1000 * MS);
    assert(executed_count == 4);
    assert(executed[1].time == start + 250 * MS && !executed[1].activate);
    assert(executed[2].time == start + 375 * MS && !executed[2].activate);
    assert(executed[3].time == start + 500 * MS && executed[3].activate);
    assert(executed[1].duration == SPACE_NAVIGATION_BURST_S && executed[2].duration == SPACE_NAVIGATION_BURST_S);
    assert(executed[3].duration == 0.25f);

    for (int i = 1; i < 4; ++i) {
        assert(executed[i].steps == 1);
    }

    // The window fade's last step keeps its own duration.
    reset();
    press(-1, false, false);
    advance(20 * MS);
    press(-1, false, false);
    advance(1000 * MS);
    assert(executed_count == 2 && executed[1].steps == -1 && executed[1].duration == 0.25f);

    // Separate taps, each alone in the queue, keep the requested duration.
    reset();
    press(1, false, true);
    advance(150 * MS);
    press(1, false, true);
    advance(400 * MS);
    press(1, false, true);
    assert(executed_count == 3);
    assert(executed[0].duration == 0.25f);
    assert(executed[1].duration == 0.25f);
    assert(executed[2].duration == 0.25f);

    // A shorter requested duration is never lengthened.
    reset();
    struct space_navigation_request quick = relative(1, true);
    quick.duration = 0.05f;
    assert(space_navigation_schedule_add(&quick, false));
    assert(space_navigation_schedule_add(&quick, false));
    assert(space_navigation_schedule_add(&quick, false));
    space_navigation_schedule_pump();
    advance(1000 * MS);
    assert(executed_count == 3 && executed[0].duration == 0.05f && executed[1].duration == 0.05f);
}

static void test_activation(void)
{
    // A confirmed activation lets the next step run at the rhythm.
    reset();
    uint64_t start = now;
    activate_window = 7;
    press(1, false, false);
    advance(20 * MS);
    press(1, false, false);
    assert(executed_count == 1);

    advance(40 * MS);
    space_navigation_schedule_focused(8);
    assert(g_space_navigation_schedule.activated == 7);
    space_navigation_schedule_focused(7);
    assert(executed_count == 1 && !g_space_navigation_schedule.activated);

    advance(40 * MS);
    assert(executed_count == 2 && executed[1].time == start + 100 * MS);

    // A step that activated nothing does not wait.
    reset();
    start = now;
    press(1, false, false);
    advance(20 * MS);
    press(1, false, false);
    advance(80 * MS);
    assert(executed_count == 2 && executed[1].time == start + 100 * MS);
}

static void test_slow_step(void)
{
    // A switch or AX raise can occupy the event loop for longer than the
    // rhythm. A newly admitted request must not bypass the scheduled pause
    // and run immediately when that slow step returns.
    reset();
    uint64_t start = now;
    execute_delay = 300 * MS;
    press(1, false, false);
    assert(now == start + 300 * MS && executed_count == 1);

    execute_delay = 0;
    press(1, false, false);
    assert(executed_count == 1);
    advance(99 * MS);
    assert(executed_count == 1);
    advance(MS);
    assert(executed_count == 2 && executed[1].time == start + 400 * MS);
}

static void test_focus_confirmation_defers_navigation(void)
{
    // WINDOW_FOCUSED still has to process the old window after notifying
    // the scheduler. Starting the next navigation inside that notification
    // would let the rest of the old event overwrite the new focus state.
    reset();
    activate_window = 7;
    press(1, false, false);
    press(1, false, false);
    now += 120 * MS;

    space_navigation_schedule_focused(7);
    assert(executed_count == 1);
    advance(0);
    assert(executed_count == 2);
}

static void test_repeats_and_reversal(void)
{
    // A held key keeps one step pending: at 0 ms, when the first blend ends
    // at 250 ms, and once more after the release at 300 ms, 125 ms later.
    // The steps a repeat queued blend within the burst's pace.
    reset();
    press(1, false, true);
    for (int i = 0; i < 10; ++i) {
        advance(30 * MS);
        press(1, true, true);
    }

    assert(executed_count == 2);
    advance(1000 * MS);
    assert(executed_count == 3);
    assert(executed[0].duration == 0.25f);
    assert(executed[1].duration == SPACE_NAVIGATION_BURST_S && executed[1].activate);
    assert(executed[2].duration == SPACE_NAVIGATION_BURST_S && executed[2].activate);
    assert(executed[2].time == executed[1].time + 125 * MS);

    // The other direction takes back a step that has not run.
    reset();
    press(1, false, true);
    advance(10 * MS);
    press(1, false, true);
    advance(10 * MS);
    press(-1, false, true);
    assert(g_space_navigation_schedule.count == 0);
    advance(1000 * MS);
    assert(executed_count == 1);

    // A repeat after the queue has run adds a step.
    reset();
    press(1, false, true);
    advance(300 * MS);
    press(1, true, true);
    assert(executed_count == 2);
}

static void test_absolute(void)
{
    // Desktop numbers run in order; the same one twice queues once.
    reset();
    go_to(3);
    advance(10 * MS);
    go_to(5);
    go_to(5);
    advance(10 * MS);
    go_to(2);
    assert(g_space_navigation_schedule.count == 2);

    advance(1000 * MS);
    assert(executed_count == 3);
    assert(executed[0].sid == 3 && executed[1].sid == 5 && executed[2].sid == 2);
    assert(executed[0].steps == 0 && !executed[1].activate && executed[2].activate);

    // Relative steps after a number run after it.
    reset();
    go_to(3);
    advance(10 * MS);
    go_to(6);
    press(1, false, true);
    advance(1000 * MS);
    assert(executed_count == 3 && executed[1].sid == 6 && executed[2].steps == 1);
}

static void test_overflow(void)
{
    // At most ten switches wait. Steps beyond them join a jump at the end:
    // every press still counts, and navigation still ends in time.
    reset();
    press(1, false, true);

    struct space_navigation_request nine = relative(9, true);
    struct space_navigation_request one = relative(1, true);
    struct space_navigation_request back = relative(-1, true);

    assert(space_navigation_schedule_add(&nine, false));
    assert(space_navigation_schedule_add(&one, false));
    assert(space_navigation_schedule_steps() == 10 && g_space_navigation_schedule.count == 1);

    assert(space_navigation_schedule_add(&one, false));
    assert(space_navigation_schedule_steps() == 10 && g_space_navigation_schedule.count == 2);
    assert(g_space_navigation_schedule.queue[0].steps == 9 && !g_space_navigation_schedule.queue[0].jump);
    assert(g_space_navigation_schedule.queue[1].steps == 2 && g_space_navigation_schedule.queue[1].jump);

    assert(space_navigation_schedule_add(&one, false));
    assert(space_navigation_schedule_add(&one, false));
    assert(g_space_navigation_schedule.queue[1].steps == 4 && space_navigation_schedule_steps() == 10);

    // A held key still keeps one step pending, and the other direction
    // takes one back from the jump.
    assert(space_navigation_schedule_add(&one, true));
    assert(g_space_navigation_schedule.queue[1].steps == 4);
    assert(space_navigation_schedule_add(&back, false));
    assert(g_space_navigation_schedule.queue[1].steps == 3);

    space_navigation_schedule_pump();
    advance(5000 * MS);
    assert(executed_count == 11 && executed_steps() == 13);
    assert(executed[10].steps == 3 && executed[10].activate && executed[10].duration == 0.25f);
    for (int i = 1; i < 10; ++i) {
        assert(executed[i].steps == 1 && !executed[i].activate);
    }

    // Presses merged into one request split the same way.
    reset();
    struct space_navigation_request fourteen = relative(14, true);
    assert(space_navigation_schedule_add(&fourteen, false));
    assert(g_space_navigation_schedule.count == 2 && space_navigation_schedule_steps() == 10);
    space_navigation_schedule_pump();
    advance(5000 * MS);
    assert(executed_count == 10 && executed_steps() == 14 && executed[9].steps == 5);

    // A different effect makes a jump of its own when one switch is left.
    reset();
    press(1, false, true);
    assert(space_navigation_schedule_add(&nine, false));
    struct space_navigation_request fade = relative(3, false);
    assert(space_navigation_schedule_add(&fade, false));
    assert(g_space_navigation_schedule.count == 2 && g_space_navigation_schedule.queue[1].jump);
    assert(g_space_navigation_schedule.queue[1].steps == 3 && space_navigation_schedule_steps() == 10);
    fade.steps = 1;
    assert(space_navigation_schedule_add(&fade, false));
    assert(g_space_navigation_schedule.queue[1].steps == 4);

    // The other direction can empty a jump, and then reach the steps before it.
    reset();
    struct space_navigation_request eleven = relative(11, true);
    assert(space_navigation_schedule_add(&eleven, false));
    for (int i = 0; i < 3; ++i) {
        assert(space_navigation_schedule_add(&back, false));
    }

    assert(g_space_navigation_schedule.count == 1 && g_space_navigation_schedule.queue[0].steps == 8);
    assert(!g_space_navigation_schedule.queue[0].jump);
}

static void test_full_queue(void)
{
    // A Desktop number takes the place of the last switch of a full queue:
    // navigation ends where the latest press asked.
    reset();
    press(1, false, true);

    struct space_navigation_request ten = relative(10, true);
    assert(space_navigation_schedule_add(&ten, false));

    struct space_navigation_request four = number(4, false);
    assert(space_navigation_schedule_add(&four, false));
    assert(g_space_navigation_schedule.count == 2 && space_navigation_schedule_steps() == 10);
    assert(g_space_navigation_schedule.queue[0].steps == 9 && g_space_navigation_schedule.queue[1].sid == 4);

    struct space_navigation_request six = number(6, false);
    assert(space_navigation_schedule_add(&six, false));
    assert(g_space_navigation_schedule.count == 2 && g_space_navigation_schedule.queue[1].sid == 6);

    // A window move is never dropped in favour of another request, and a
    // request that can join nothing is refused.
    struct space_navigation_request move = number(5, true);
    assert(!space_navigation_schedule_add(&move, false));

    struct space_navigation_request fade = relative(1, false);
    assert(!space_navigation_schedule_add(&fade, false));
    assert(g_space_navigation_schedule.queue[1].sid == 6);

    space_navigation_schedule_pump();
    advance(5000 * MS);
    assert(executed_count == 11 && executed[10].sid == 6 && executed[10].activate);

    // A queued move is not replaced either.
    reset();
    press(1, false, true);
    struct space_navigation_request nine = relative(9, true);
    assert(space_navigation_schedule_add(&nine, false));
    struct space_navigation_request moved = number(3, true);
    assert(space_navigation_schedule_add(&moved, false));
    assert(space_navigation_schedule_steps() == 10);
    assert(!space_navigation_schedule_add(&four, false));
}

static void test_cancellation(void)
{
    // A click after a request drops the queue.
    reset();
    press(1, false, true);
    advance(10 * MS);
    press(1, false, true);
    advance(10 * MS);
    click_seconds = 0.005;
    advance(1000 * MS);
    assert(executed_count == 1 && g_space_navigation_schedule.count == 0);

    // So does another command, and a failed step.
    reset();
    press(1, false, true);
    advance(10 * MS);
    press(2, false, true);
    space_navigation_schedule_cancel();
    advance(1000 * MS);
    assert(executed_count == 1);

    reset();
    press(1, false, true);
    advance(10 * MS);
    press(3, false, true);
    execute_success = false;
    advance(1000 * MS);
    assert(executed_count == 2 && g_space_navigation_schedule.count == 0);

    // One wake at a time, and an earlier one only when a confirmation brings
    // the next step forward.
    reset();
    activate_window = 7;
    press(1, false, true);
    advance(10 * MS);
    press(1, false, true);
    press(1, false, true);
    assert(pending_wakes() == 1);
    space_navigation_schedule_focused(7);
    assert(pending_wakes() == 2);
    advance(1000 * MS);
    assert(executed_count == 3 && pending_wakes() == 0);

    // Turning pacing off empties the queue.
    reset();
    press(1, false, true);
    press(1, false, true);
    space_navigation_schedule_set_pacing(false);
    assert(!space_navigation_schedule_pacing() && g_space_navigation_schedule.count == 0);
}

int main(void)
{
    test_isolated_and_burst();
    test_activation();
    test_slow_step();
    test_focus_confirmation_defers_navigation();
    test_repeats_and_reversal();
    test_absolute();
    test_overflow();
    test_full_queue();
    test_cancellation();

    puts("navigation schedule: rhythm, burst pace, activation wait, repeats, order, overflow and cancellation checks passed");
    return 0;
}
