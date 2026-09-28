// Paces Desktop navigation, one Desktop at a time.
//
// Requests can arrive faster than a switch completes: a held key repeats every
// 30 ms, and a busy Edge or VS Code handles an activation hundreds of
// milliseconds after it was sent. Each request becomes steps of one Desktop
// in a bounded queue, run in order and at most one every
// SPACE_NAVIGATION_RHYTHM_NS. After a step that activated an application, the
// next one also waits until that application reports the window focused, or
// until SPACE_NAVIGATION_ACTIVATION_NS has passed: an activation handled after
// the user had moved on brought its Desktop back into view.
//
// Only the last queued step activates an application; the steps before it
// switch and show their effect. A step that finds more queued behind it when
// it comes to activate, as presses that arrived while it captured, leaves the
// activation to the last of them too. A Desktop reached without activation,
// whose queue opposite presses then emptied, takes focus without a switch
// once SPACE_NAVIGATION_SETTLE_NS have passed without a request.
//
// The next step waits for the effect's duration, measured from Dock's
// acknowledgement, so a blend always finishes before the next one starts. A
// step with more queued behind it, or one a held key repeats, blends within
// SPACE_NAVIGATION_BURST_S, so that scrolling through the Desktops keeps a
// quick, regular pace; the last of separate presses keeps the requested
// duration. A held key's last step cannot: when it runs, the key may still be
// down. A click after a request, or any other command, empties the queue.
//
// At most SPACE_NAVIGATION_QUEUE_STEPS switches wait, which bounds how long
// navigation goes on after the last press. No press is dropped for that:
// relative steps beyond the bound join a jump at the end of the queue, one
// switch over several Desktops, a Desktop number takes the place of the last
// switch, and relative steps after a Desktop number jump on from it.
// Navigation still ends where the presses asked.

#include <os/signpost.h>

#define SPACE_NAVIGATION_RHYTHM_NS     100000000ULL
#define SPACE_NAVIGATION_ACTIVATION_NS 150000000ULL
#define SPACE_NAVIGATION_BURST_S       0.125f
#define SPACE_NAVIGATION_QUEUE_STEPS   10
#define SPACE_NAVIGATION_SETTLE_NS     150000000ULL

static struct
{
    bool pacing;

    struct space_navigation_request queue[SPACE_NAVIGATION_QUEUE_STEPS];
    int count;

    uint64_t last_step;     // Completion of the last step, including synchronous focus work.
    uint64_t effect_until;  // Conservative end time after Dock acknowledged a crossfade.
    uint32_t activated;     // The window the last step activated, until it reports focus.
    uint64_t activated_at;
    uint64_t confirmed_at;  // When the last activation reported focus.
    bool unsettled;         // The last step switched and left activation to the next.
    uint64_t last_request;  // When the last request was queued.
    bool running;           // The step the pump started has not ended, as while it waits for its capture.
    bool running_activates; // That step activates.

    // When the earliest wake requested is due, 0 when none is.
    uint64_t timer;
} g_space_navigation_schedule = { .pacing = true };

// Signposts for Instruments, subsystem com.lcs.yabai: requests, steps,
// activations and the focus that confirms them, on the timeline of
// WindowServer's frames. Only the event loop uses them.
static os_log_t space_navigation_log(void)
{
    static os_log_t log;
    if (!log) log = os_log_create("com.lcs.yabai", "navigation");
    return log;
}

static bool space_navigation_schedule_pacing(void)
{
    return g_space_navigation_schedule.pacing;
}

static void space_navigation_schedule_clear(void)
{
    g_space_navigation_schedule.count = 0;
    g_space_navigation_schedule.unsettled = false;
}

static void space_navigation_schedule_set_pacing(bool pacing)
{
    g_space_navigation_schedule.pacing = pacing;
    space_navigation_schedule_clear();
}

// The switches a queued request still makes: one for a Desktop number or a jump.
static int space_navigation_schedule_size(struct space_navigation_request *request)
{
    return request->steps && !request->jump ? abs(request->steps) : 1;
}

static int space_navigation_schedule_steps(void)
{
    int steps = 0;

    for (int i = 0; i < g_space_navigation_schedule.count; ++i) {
        steps += space_navigation_schedule_size(&g_space_navigation_schedule.queue[i]);
    }

    return steps;
}

static bool space_navigation_schedule_same_effect(struct space_navigation_request *a, struct space_navigation_request *b)
{
    return a->move == b->move && a->crossfade == b->crossfade
        && a->alpha == b->alpha && a->duration == b->duration;
}

// Appends a request as at most `budget` switches, budget >= 1. A relative
// request with more steps makes budget - 1 of them one at a time and the rest
// in a final jump. There is room for both: every queued request makes at
// least one switch, so at most SPACE_NAVIGATION_QUEUE_STEPS - budget are queued.
static void space_navigation_schedule_append(struct space_navigation_request *request, int budget)
{
    struct space_navigation_request *queue = g_space_navigation_schedule.queue;

    if (space_navigation_schedule_size(request) <= budget) {
        queue[g_space_navigation_schedule.count++] = *request;
        return;
    }

    int direction = request->steps > 0 ? 1 : -1;
    int single = budget - 1;

    if (single) {
        queue[g_space_navigation_schedule.count] = *request;
        queue[g_space_navigation_schedule.count].steps = direction * single;
        ++g_space_navigation_schedule.count;
    }

    queue[g_space_navigation_schedule.count] = *request;
    queue[g_space_navigation_schedule.count].steps = request->steps - direction * single;
    queue[g_space_navigation_schedule.count].jump = true;
    ++g_space_navigation_schedule.count;
}

// Queues a request. A key repeat only keeps one step pending in its
// direction; an opposite step takes back one that has not run. Returns false
// only when the queue is full and the request can join nothing: a `move`,
// or a different effect.
static bool space_navigation_schedule_add(struct space_navigation_request *request, bool repeat)
{
    os_signpost_event_emit(space_navigation_log(), OS_SIGNPOST_ID_EXCLUSIVE, "request",
                           "steps %d sid %llu repeat %d queued %d", request->steps, request->sid, repeat,
                           g_space_navigation_schedule.count);

    struct space_navigation_request entry = *request;
    entry.jump = false;
    entry.repeat = repeat;
    entry.time = read_os_timer();
    g_space_navigation_schedule.last_request = entry.time;

    int count = g_space_navigation_schedule.count;
    struct space_navigation_request *tail = count ? &g_space_navigation_schedule.queue[count - 1] : NULL;
    bool same = tail && space_navigation_schedule_same_effect(tail, &entry);

    // Relative steps add up with those of the last request, which is taken
    // out and queued again with their sum.
    if (entry.steps && same && tail->steps) {
        if (repeat && tail->steps * entry.steps > 0) return true;

        struct space_navigation_request joined = *tail;
        joined.steps += entry.steps;
        --g_space_navigation_schedule.count;

        // Steps that cancel out a jump from a Desktop number leave the number.
        if (!joined.steps) joined.jump = false;

        if (joined.steps || joined.sid) {
            int budget = SPACE_NAVIGATION_QUEUE_STEPS - space_navigation_schedule_steps();
            space_navigation_schedule_append(&joined, budget);
        }

        return true;
    }

    // The same Desktop number twice queues once.
    if (!entry.steps && same && !tail->steps && tail->sid == entry.sid) return true;

    int budget = SPACE_NAVIGATION_QUEUE_STEPS - space_navigation_schedule_steps();
    if (budget > 0) {
        space_navigation_schedule_append(&entry, budget);
        return true;
    }

    // The queue is full. A Desktop number takes the place of the last switch
    // of a focus request, which navigation would have left at once anyway.
    if (tail && !entry.steps && !entry.move && !tail->move) {
        if (space_navigation_schedule_size(tail) > 1) {
            tail->steps -= tail->steps > 0 ? 1 : -1;
            g_space_navigation_schedule.queue[g_space_navigation_schedule.count++] = entry;
        } else {
            *tail = entry;
        }

        return true;
    }

    // Relative steps after a Desktop number jump on from it. A held key's
    // repeats wait, as they do behind any step.
    if (entry.steps && same && !tail->steps && !entry.move) {
        if (!repeat) {
            tail->steps = entry.steps;
            tail->jump = true;
        }

        return true;
    }

    return false;
}

// A confirmed activation can make the next step due before the wake already
// requested for its deadline, so an earlier wake is requested too. The later
// one then finds nothing due.
static void space_navigation_schedule_wake(uint64_t delay_ns)
{
    uint64_t due = read_os_timer() + delay_ns;
    if (g_space_navigation_schedule.timer && g_space_navigation_schedule.timer <= due) return;

    g_space_navigation_schedule.timer = due;
    space_navigation_schedule_after(delay_ns);
}

// Starts a step, which reports its end to space_navigation_schedule_completed
// at once or once its capture came back.
static void space_navigation_schedule_run(struct space_navigation_request *request, int steps, bool activate,
                                          bool settle, float duration, double late)
{
    g_space_navigation_schedule.activated = 0;
    g_space_navigation_schedule.last_step = read_os_timer();

    os_signpost_interval_begin(space_navigation_log(), OS_SIGNPOST_ID_EXCLUSIVE, "step",
                               "steps %d sid %llu activate %d duration %.3f late %.1f ms",
                               steps, request->sid, activate, duration, late);

    g_space_navigation_schedule.running = true;
    g_space_navigation_schedule.running_activates = activate;

    enum space_navigation_result result = space_navigation_execute(request, steps, activate, settle, duration);
    if (result != SPACE_NAVIGATION_PENDING) space_navigation_schedule_completed(result == SPACE_NAVIGATION_SWITCHED);
}

// The queue is empty and the Desktop the last step reached has no focus of
// its own. A click since the last request decided the focus instead.
static void space_navigation_schedule_settle(uint64_t now)
{
    uint64_t quiet = now - g_space_navigation_schedule.last_request;
    if (space_navigation_seconds_since_click() * 1e9 < (double) quiet) {
        g_space_navigation_schedule.unsettled = false;
        return;
    }

    uint64_t due = g_space_navigation_schedule.last_request + SPACE_NAVIGATION_SETTLE_NS;
    if (now < due) {
        space_navigation_schedule_wake(due - now);
        return;
    }

    // Neither steps nor a Desktop: the step stays on the Desktop navigation
    // reached and gives it focus. It settles once, whether or not it succeeds.
    g_space_navigation_schedule.unsettled = false;
    struct space_navigation_request request = { .time = now };
    space_navigation_schedule_run(&request, 0, true, true, 0.0f, (now - due) / 1e6);
}

// Runs the next step when it is due, or asks to be woken when it will be.
static void space_navigation_schedule_pump(void)
{
    if (g_space_navigation_schedule.running) return;

    uint64_t now = read_os_timer();

    if (!g_space_navigation_schedule.count) {
        if (g_space_navigation_schedule.unsettled) space_navigation_schedule_settle(now);
        return;
    }

    struct space_navigation_request *head = &g_space_navigation_schedule.queue[0];

    if (space_navigation_seconds_since_click() * 1e9 < (double) (now - head->time)) {
        space_navigation_schedule_clear();
        return;
    }

    uint64_t due = g_space_navigation_schedule.last_step + SPACE_NAVIGATION_RHYTHM_NS;
    if (g_space_navigation_schedule.effect_until > due) due = g_space_navigation_schedule.effect_until;

    if (g_space_navigation_schedule.activated) {
        uint64_t deadline = g_space_navigation_schedule.activated_at + SPACE_NAVIGATION_ACTIVATION_NS;
        if (deadline > due) due = deadline;
    }

    if (now < due) {
        space_navigation_schedule_wake(due - now);
        return;
    }

    // How long the step waited after it could run, while the event loop was
    // busy with other events.
    uint64_t runnable = due > head->time ? due : head->time;
    if (g_space_navigation_schedule.confirmed_at > runnable) runnable = g_space_navigation_schedule.confirmed_at;
    double late = (now - runnable) / 1e6;

    struct space_navigation_request request = *head;
    int direction = request.steps > 0 ? 1 : request.steps < 0 ? -1 : 0;
    int steps = request.jump ? request.steps : direction;

    if (!request.jump && direction && request.steps != direction) {
        head->steps -= direction;
    } else {
        --g_space_navigation_schedule.count;
        memmove(head, head + 1, g_space_navigation_schedule.count * sizeof(*head));
    }

    bool activate = g_space_navigation_schedule.count == 0;
    bool settle = activate && g_space_navigation_schedule.unsettled;
    float duration = request.duration;
    if ((!activate || request.repeat) && duration > SPACE_NAVIGATION_BURST_S) duration = SPACE_NAVIGATION_BURST_S;

    space_navigation_schedule_run(&request, steps, activate, settle, duration, late);
}

// The running step is about to activate. Steps queued behind it since it
// started make it leave the activation to the last of them.
static bool space_navigation_schedule_defers_activation(void)
{
    if (!g_space_navigation_schedule.running || !g_space_navigation_schedule.count) return false;

    g_space_navigation_schedule.running_activates = false;
    return true;
}

// The step the pump started has ended: at once, or once its capture came
// back. A step abandoned by space_navigation_schedule_cancel never reports.
static void space_navigation_schedule_completed(bool success)
{
    if (!g_space_navigation_schedule.running) return;
    g_space_navigation_schedule.running = false;

    // A failed step leaves the Desktop the steps before it reached as it was,
    // without focus when the last of them deferred its activation.
    bool unsettled = success ? !g_space_navigation_schedule.running_activates : g_space_navigation_schedule.unsettled;
    // A slow WindowServer query or AX raise can consume the whole rhythm.
    // Admission of the next request must obey the same pause as our timer,
    // rather than immediately executing another step to catch up.
    g_space_navigation_schedule.last_step = read_os_timer();
    os_signpost_interval_end(space_navigation_log(), OS_SIGNPOST_ID_EXCLUSIVE, "step", "success %d", success);

    if (!success) {
        debug("%s: navigation step failed, dropping %d queued\n", __FUNCTION__, g_space_navigation_schedule.count);
        space_navigation_schedule_clear();
    }

    g_space_navigation_schedule.unsettled = unsettled;

    // The wake settles a Desktop left without focus when it is due, never
    // from within the step that ends here.
    if (g_space_navigation_schedule.count) {
        space_navigation_schedule_wake(SPACE_NAVIGATION_RHYTHM_NS);
    } else if (g_space_navigation_schedule.unsettled) {
        space_navigation_schedule_wake(0);
    }
}

// Event loop, when a requested delay has passed. The pump asks again for
// whatever is still waiting.
static void space_navigation_schedule_timer(void)
{
    g_space_navigation_schedule.timer = 0;
    space_navigation_schedule_pump();
}

static void space_navigation_schedule_activated(uint32_t window_id)
{
    os_signpost_event_emit(space_navigation_log(), OS_SIGNPOST_ID_EXCLUSIVE, "activated", "window %u", window_id);
    g_space_navigation_schedule.activated = window_id;
    g_space_navigation_schedule.activated_at = read_os_timer();
}

// Dock has accepted the effect. This is a time guard, not proof that the
// compositor has presented its last frame. A refused effect clears the guard
// when navigation falls back to an ordinary switch.
static void space_navigation_schedule_switched(float duration)
{
    g_space_navigation_schedule.effect_until = read_os_timer() + (uint64_t) (duration * 1e9);
}

// The application took focus on the window the last step activated.
static void space_navigation_schedule_focused(uint32_t window_id)
{
    if (!window_id || window_id != g_space_navigation_schedule.activated) return;

    os_signpost_event_emit(space_navigation_log(), OS_SIGNPOST_ID_EXCLUSIVE, "focused", "window %u", window_id);
    g_space_navigation_schedule.activated = 0;
    g_space_navigation_schedule.confirmed_at = read_os_timer();

    // Finish WINDOW_FOCUSED before changing Desktop again. Its remaining
    // work belongs to the old window and must precede the next activation.
    if (g_space_navigation_schedule.count) space_navigation_schedule_wake(0);
}

// Another command abandons the queue and the step in flight; command
// abandons that step itself.
static void space_navigation_schedule_cancel(void)
{
    space_navigation_schedule_clear();

    if (g_space_navigation_schedule.running) {
        g_space_navigation_schedule.running = false;
        os_signpost_interval_end(space_navigation_log(), OS_SIGNPOST_ID_EXCLUSIVE, "step", "cancelled");
    }
}
