// Fork: paces Desktop navigation, one Desktop at a time.
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
// switch and show their effect. A crossfade lasts as long as the interval
// between steps, up to the requested duration, so each one ends as the next
// begins. A click after a request, or any other command, empties the queue.

#include <os/signpost.h>

#define SPACE_NAVIGATION_RHYTHM_NS     100000000ULL
#define SPACE_NAVIGATION_ACTIVATION_NS 150000000ULL
#define SPACE_NAVIGATION_QUEUE_STEPS   10

struct space_navigation_request
{
    bool move;
    int steps;          // Desktops forward (positive) or back; 0 for `sid`.
    uint64_t sid;
    bool crossfade;
    float alpha;
    float duration;
    uint64_t time;      // When it was queued.
};

static struct
{
    bool pacing;

    struct space_navigation_request queue[SPACE_NAVIGATION_QUEUE_STEPS];
    int count;

    uint64_t last_step;
    uint32_t activated;     // The window the last step activated, until it reports focus.
    uint64_t activated_at;
    uint64_t confirmed_at;  // When the last activation reported focus.

    // When the earliest wake requested is due, 0 when none is.
    uint64_t timer;
} g_space_navigation_schedule = { .pacing = true };

static bool space_navigation_execute(struct space_navigation_request *request, int direction,
                                     bool activate, float duration);
static void space_navigation_schedule_after(uint64_t delay_ns);

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

static void space_navigation_schedule_set_pacing(bool pacing)
{
    g_space_navigation_schedule.pacing = pacing;
    g_space_navigation_schedule.count = 0;
}

static int space_navigation_schedule_size(struct space_navigation_request *request)
{
    return request->steps ? abs(request->steps) : 1;
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

// Queues a request. A key repeat only keeps one step pending in its
// direction; an opposite step takes back one that has not run. Returns false
// when the queue has no room for it.
static bool space_navigation_schedule_add(struct space_navigation_request *request, bool repeat)
{
    os_signpost_event_emit(space_navigation_log(), OS_SIGNPOST_ID_EXCLUSIVE, "request",
                           "steps %d sid %llu repeat %d queued %d", request->steps, request->sid, repeat,
                           g_space_navigation_schedule.count);

    int count = g_space_navigation_schedule.count;
    struct space_navigation_request *tail = count ? &g_space_navigation_schedule.queue[count - 1] : NULL;
    bool same = tail && space_navigation_schedule_same_effect(tail, request);

    if (request->steps && same && tail->steps) {
        if (repeat && tail->steps * request->steps > 0) return true;

        int size = abs(tail->steps + request->steps) - abs(tail->steps);
        if (space_navigation_schedule_steps() + size > SPACE_NAVIGATION_QUEUE_STEPS) return false;

        tail->steps += request->steps;
        if (!tail->steps) --g_space_navigation_schedule.count;

        return true;
    }

    if (!request->steps && same && !tail->steps && tail->sid == request->sid) return true;

    int size = space_navigation_schedule_size(request);
    if (count == SPACE_NAVIGATION_QUEUE_STEPS
        || space_navigation_schedule_steps() + size > SPACE_NAVIGATION_QUEUE_STEPS) {
        return false;
    }

    g_space_navigation_schedule.queue[count] = *request;
    g_space_navigation_schedule.queue[count].time = read_os_timer();
    ++g_space_navigation_schedule.count;

    return true;
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

// Runs the next step when it is due, or asks to be woken when it will be.
static void space_navigation_schedule_pump(void)
{
    if (!g_space_navigation_schedule.count) return;

    uint64_t now = read_os_timer();
    struct space_navigation_request *head = &g_space_navigation_schedule.queue[0];

    if (space_navigation_seconds_since_click() * 1e9 < (double) (now - head->time)) {
        g_space_navigation_schedule.count = 0;
        return;
    }

    uint64_t due = g_space_navigation_schedule.last_step + SPACE_NAVIGATION_RHYTHM_NS;

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

    if (direction && request.steps != direction) {
        head->steps -= direction;
    } else {
        --g_space_navigation_schedule.count;
        memmove(head, head + 1, g_space_navigation_schedule.count * sizeof(*head));
    }

    // A crossfade lasts until the next step can start, up to the request.
    float duration = request.duration;

    if (request.crossfade) {
        uint64_t interval = g_space_navigation_schedule.count ? SPACE_NAVIGATION_RHYTHM_NS
                                                              : now - g_space_navigation_schedule.last_step;
        if (interval < SPACE_NAVIGATION_RHYTHM_NS) interval = SPACE_NAVIGATION_RHYTHM_NS;
        if (interval / 1e9 < duration) duration = (float) (interval / 1e9);
    }

    g_space_navigation_schedule.activated = 0;
    g_space_navigation_schedule.last_step = now;

    bool activate = g_space_navigation_schedule.count == 0;
    os_signpost_interval_begin(space_navigation_log(), OS_SIGNPOST_ID_EXCLUSIVE, "step",
                               "direction %d sid %llu activate %d duration %.3f late %.1f ms",
                               direction, request.sid, activate, duration, late);

    bool success = space_navigation_execute(&request, direction, activate, duration);
    os_signpost_interval_end(space_navigation_log(), OS_SIGNPOST_ID_EXCLUSIVE, "step", "success %d", success);

    if (!success) {
        debug("%s: navigation step failed, dropping %d queued\n", __FUNCTION__, g_space_navigation_schedule.count);
        g_space_navigation_schedule.count = 0;
    }

    if (g_space_navigation_schedule.count) space_navigation_schedule_wake(SPACE_NAVIGATION_RHYTHM_NS);
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

// The application took focus on the window the last step activated.
static void space_navigation_schedule_focused(uint32_t window_id)
{
    if (!window_id || window_id != g_space_navigation_schedule.activated) return;

    os_signpost_event_emit(space_navigation_log(), OS_SIGNPOST_ID_EXCLUSIVE, "focused", "window %u", window_id);
    g_space_navigation_schedule.activated = 0;
    g_space_navigation_schedule.confirmed_at = read_os_timer();
    space_navigation_schedule_pump();
}

static void space_navigation_schedule_cancel(void)
{
    g_space_navigation_schedule.count = 0;
}
