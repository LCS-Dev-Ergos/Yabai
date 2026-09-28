// Reports the events that hold up the event loop.
//
// One thread handles every event in order, so an event that takes long
// delays everything queued behind it, paced navigation steps included. Each
// event handled in more than EVENT_LOOP_SLOW_NS emits a signpost in subsystem
// com.lcs.yabai, category events, with its type and how long it took.

#include <os/signpost.h>

#define EVENT_LOOP_SLOW_NS 10000000ULL

static const char *event_loop_type_names[] = {
#define EVENT_TYPE_ENTRY(value) #value,
    EVENT_TYPE_LIST
#undef EVENT_TYPE_ENTRY
};

static void event_loop_trace(enum event_type type, uint64_t started)
{
    uint64_t elapsed = read_os_timer() - started;
    if (elapsed < EVENT_LOOP_SLOW_NS) return;

    static os_log_t log;
    if (!log) log = os_log_create("com.lcs.yabai", "events");

    os_signpost_event_emit(log, OS_SIGNPOST_ID_EXCLUSIVE, "slow event", "%{public}s %.1f ms",
                           event_loop_type_names[type], elapsed / 1e6);
}
