// Exercise the actual socket admission, token parser, command and schedule.
// Host services use six deterministic Desktops; no daemon or Dock is touched.
#include "../../src/core_prelude.h"
#include <assert.h>

static uint64_t now = 10000000000ULL;
static uint64_t current = 1;
static uint64_t target[32];
static float durations[32];
static bool activations[32], crossfades[32], veils[32];
static char last_reply[256];
static int switches, skips;
static double click_seconds = 1000.0;
static uint64_t dispatch_delay;

#undef debug
#define debug(...)
static uint64_t ingress_time(void) { return now; }
#define read_os_timer() ingress_time()
#define CGEventSourceSecondsSinceLastEventType(state, type) (1000.0)
#include "../../src/ipc/message.c"
#include "../../src/navigation/admission.c"

struct space_manager g_space_manager;
uint32_t display_manager_active_display_id(void) { return 1; }
uint64_t space_manager_mission_control_space(int index) { return index >= 1 && index <= 6 ? index : 0; }
uint64_t space_manager_first_space(void) { return 1; }
uint64_t space_manager_last_space(void) { return 6; }
uint64_t space_manager_next_space(uint64_t sid) { return sid < 6 ? sid + 1 : 0; }
uint64_t space_manager_prev_space(uint64_t sid) { return sid > 1 ? sid - 1 : 0; }
uint64_t space_manager_cursor_space(void) { return current; }
struct space_label *space_manager_get_space_for_label(struct space_manager *sm, char *label) { return NULL; }
static void space_navigation_spaces_read(void) { }
static void space_navigation_spaces_unload(void) { }
static bool space_navigation_spaces_loaded(void) { return true; }
static uint64_t space_navigation_spaces_at(int index) { return space_manager_mission_control_space(index); }
static uint64_t space_navigation_spaces_offset(uint64_t sid, int steps) { return ((int)sid - 1 + steps + 600) % 6 + 1; }
static uint64_t space_navigation_display_space(uint32_t did) { return current; }
static uint64_t space_navigation_current_space(uint64_t sid) { return sid; }
static double space_navigation_seconds_since_click(void) { return click_seconds; }
static void space_navigation_step_cancel(void) { }
void space_navigation_snapshot_cancel(void) { }
static void space_navigation_forget(void) { }
static void space_navigation_focus_cancel(void) { }
static void space_navigation_step_skip_effect(void) { ++skips; }
static bool space_navigation_step_replace_after_click(uint64_t input_time) { return false; }
static enum space_navigation_result space_navigation_begin_step(uint64_t from, struct space_navigation_step *step)
{
    assert(switches < 32);
    target[switches] = step->sid;
    durations[switches] = step->duration;
    crossfades[switches] = step->crossfade;
    veils[switches] = step->veil;
    activations[switches++] = step->activate;
    current = step->sid;
    space_navigation_schedule_switched(step->duration);
    return SPACE_NAVIGATION_SWITCHED;
}
static int veil_runs;
static bool last_run_veil, last_run_crossfade;
static float last_run_duration;
static bool space_navigation_run_step(uint64_t from, struct space_navigation_step *step)
{
    veil_runs += step->veil;
    last_run_veil = step->veil;
    last_run_crossfade = step->crossfade;
    last_run_duration = step->duration;
    return true;
}
#define event_loop_post(...) ((void)0)
#include "../../src/navigation/schedule.c"
#include "../../src/navigation/command.c"

static void reset(void)
{
    while (g_space_navigation_queue.first) space_navigation_queue_claim(g_space_navigation_queue.first->owner);
    memset(&g_space_navigation_queue.direction, 0, sizeof(g_space_navigation_queue.direction));
    g_space_navigation_queue.time = 0;
    memset(&g_space_navigation_claim, 0, sizeof(g_space_navigation_claim));
    memset(&g_space_navigation_schedule, 0, sizeof(g_space_navigation_schedule));
    g_space_navigation_schedule.pacing = true;
    char cancel[] = "window\0--focus\0";
    space_navigation_note_message(cancel);
    now += 10000000000ULL;
    current = 1;
    switches = skips = veil_runs = 0;
    click_seconds = 1000.0;
    dispatch_delay = 0;
}

static bool submit(const char *selector, const char *action, const char *effect, const char *duration)
{
    char bytes[256];
    char *cursor = bytes + sizeof(int);
    const char *args[] = { "space", "--navigate", action, selector, effect, duration };
    for (int i = 0; i < 6; ++i) {
        size_t size = strlen(args[i]) + 1;
        memcpy(cursor, args[i], size);
        cursor += size;
    }
    *cursor++ = 0;
    int size = cursor - bytes - sizeof(int);
    memcpy(bytes, &size, sizeof(size));
    int pair[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    assert(send(pair[1], bytes, sizeof(int) + size, 0) == sizeof(int) + size);
    assert(!space_navigation_accept(pair[0]));
    now += dispatch_delay;
    space_navigation_queue_claim(pair[0]);
    char *message = bytes + sizeof(int);
    get_token(&message);
    get_token(&message);
    FILE *response = tmpfile();
    assert(response);
    space_navigation_command(response, &message);
    bool accepted = ftell(response) == 0;
    memset(last_reply, 0, sizeof(last_reply));
    rewind(response);
    fread(last_reply, 1, sizeof(last_reply) - 1, response);
    fclose(response);
    close(pair[0]);
    close(pair[1]);
    return accepted;
}

static void drain(void)
{
    for (int i = 0; i < 8 && g_space_navigation_schedule.count; ++i) {
        now += SPACE_NAVIGATION_RHYTHM_NS;
        space_navigation_schedule_timer();
    }
    assert(!g_space_navigation_schedule.count);
}

static void test_mixed_and_wrapping(const char *effect)
{
    const char *first[] = { "3", "next", "6", "1" };
    const char *second[] = { "next", "5", "next", "prev" };
    uint64_t expected[] = { 4, 5, 1, 6 };
    for (int i = 0; i < 4; ++i) {
        reset();
        assert(submit(first[i], "focus", effect, "0.25"));
        now += 30000000ULL;
        assert(submit(second[i], "focus", effect, "0.25"));
        drain();
        assert(switches == 2 && target[1] == expected[i] && durations[1] == 0 && activations[1]);
    }

    reset();
    assert(submit("2", "focus", effect, "0.25"));
    now += 30000000ULL;
    assert(submit("3", "focus", effect, "0.25"));
    now += 30000000ULL;
    assert(submit("3", "focus", effect, "0.25"));
    assert(g_space_navigation_schedule.count == 1);
    drain();
    assert(switches == 2 && target[1] == 3 && activations[1]);

    // Queue delays must not hide an actual quick ingress.
    reset();
    assert(submit("3", "focus", effect, "0.25"));
    now += 30000000ULL;
    dispatch_delay = 600000000ULL;
    assert(submit("4", "focus", effect, "0.25"));
    assert(switches == 2 && durations[1] == 0);
}

// The veil is a request effect like the crossfade: it reaches the step as its
// own flag, has no duration in a quick burst, and only its exact name counts.
static void test_veil(void)
{
    reset();
    assert(submit("3", "focus", "veil", "0.25"));
    assert(switches == 1 && target[0] == 3 && durations[0] == .25f && veils[0] && !crossfades[0] && activations[0]);
    now += 30000000ULL;
    assert(submit("6", "focus", "veil", "0.25"));
    assert(g_space_navigation_schedule.queue[0].fast && g_space_navigation_schedule.queue[0].veil);
    drain();
    assert(switches == 2 && target[1] == 6 && durations[1] == 0 && veils[1] && !crossfades[1] && activations[1]);

    reset();
    assert(submit("3", "focus", "crossfade", "0.25"));
    assert(switches == 1 && crossfades[0] && !veils[0]);
    assert(submit("4", "move", "veil", "0.25"));
    drain();
    assert(switches == 2 && veils[1] && !crossfades[1] && durations[1] == .25f);

    reset();
    assert(submit("3", "focus", "0.5", "0.25"));
    assert(!crossfades[0] && !veils[0]);

    // Another word, or the right one in another case, is refused with the
    // message that names the veil.
    const char *invalid[] = { "veils", "vei", "Veil", "VEIL", "veil1", "crossfades" };
    for (int i = 0; i < 6; ++i) {
        reset();
        assert(!submit("3", "focus", invalid[i], "0.25"));
        assert(strstr(last_reply, "veil") && strstr(last_reply, "crossfade") && switches == 0);
    }

    reset();
    assert(!submit("3", "focus", "veil", "1.5"));
    assert(!submit("3", "focus", "veil", "-0.1"));
    assert(switches == 0);

    // Pacing off: the step is made at once with the flag.
    reset();
    g_space_navigation_schedule.pacing = false;
    assert(submit("3", "focus", "veil", "0.25"));
    assert(switches == 0 && veil_runs == 1 && last_run_veil && !last_run_crossfade && last_run_duration == .25f);
    g_space_navigation_schedule.pacing = true;
}

static void test_invalid_and_cancel(void)
{
    const char *invalid[] = { "0", "7", "2147483647", "2147483648", "-1", "1.0", "+1", "1x" };
    for (int i = 0; i < 8; ++i) {
        reset();
        assert(submit("3", "focus", "crossfade", "0.25"));
        now += 500000000ULL;
        assert(!submit(invalid[i], "focus", "crossfade", "0.25"));
        now += 30000000ULL;
        assert(submit("4", "focus", "crossfade", "0.25"));
        assert(switches == 2 && durations[1] == .25f);
    }

    reset();
    assert(submit("3", "focus", "crossfade", "0.25"));
    now += 500000000ULL;
    assert(!submit("4", "focus", "crossfade", "1.5"));
    now += 30000000ULL;
    assert(submit("4", "focus", "crossfade", "0.25"));
    assert(durations[1] == .25f);

    reset();
    assert(submit("3", "move", "crossfade", "0.25"));
    now += 30000000ULL;
    assert(submit("4", "focus", "crossfade", "0.25"));
    assert(!g_space_navigation_schedule.queue[0].fast);

    reset();
    assert(submit("3", "focus", "crossfade", "0.25"));
    now += 30000000ULL;
    char query[] = "query\0--spaces\0";
    space_navigation_note_message(query);
    assert(submit("4", "focus", "crossfade", "0.25"));
    assert(g_space_navigation_schedule.queue[0].fast);
    char cancel[] = "window\0--focus\0";
    space_navigation_note_message(cancel);
    now += 30000000ULL;
    assert(submit("5", "focus", "crossfade", "0.25"));
    assert(!g_space_navigation_schedule.queue[0].fast);
}

static void test_rejection(void)
{
    reset();
    assert(submit("1", "focus", "crossfade", "0.25"));
    // Hold the OS service so the queue can fill with isolated slow requests.
    g_space_navigation_schedule.running = true;
    for (int i = 2; i <= 5; ++i) {
        char selector[2] = { '0' + i, 0 };
        now += 500000000ULL;
        assert(submit(selector, "focus", "crossfade", "0.25"));
    }
    uint64_t accepted_time = g_space_navigation_schedule.last_request;
    now += 390000000ULL;
    assert(!submit("next", "focus", "0.5", "0.25"));
    assert(g_space_navigation_schedule.count == 4);
    for (int i = 0; i < 4; ++i) assert(!g_space_navigation_schedule.queue[i].fast);
    assert(g_space_navigation_schedule.last_request == accepted_time);
    now += 30000000ULL;
    assert(submit("6", "focus", "crossfade", "0.25"));
    assert(!g_space_navigation_schedule.queue[3].fast && g_space_navigation_schedule.queue[3].sid == 6);
}

int main(void)
{
    reset();
    assert(submit("3", "focus", "crossfade", "0.25"));
    assert(switches == 1 && target[0] == 3 && durations[0] == .25f);
    now += 30000000ULL;
    assert(submit("6", "focus", "crossfade", "0.25"));
    assert(g_space_navigation_schedule.queue[0].fast);
    drain();
    assert(switches == 2 && target[1] == 6 && durations[1] == 0 && activations[1]);
    test_mixed_and_wrapping("crossfade");
    test_mixed_and_wrapping("veil");
    test_veil();
    test_invalid_and_cancel();
    test_rejection();
    puts("navigation ingress: numeric and mixed bursts, veil, wrapping, validation, rejection and cancellation passed");
    return 0;
}
