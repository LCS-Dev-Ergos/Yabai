// Owns the event queue consumer, shared event flags and handler dispatch.
// The included domain handlers run on the event-loop thread.

volatile bool __pending_window_focus;
volatile uint32_t __pending_window_focus_id;
volatile bool __pending_gesture;
volatile uint64_t __last_gesture_time;
volatile uint64_t __last_cmd_tab_time;

void update_window_notifications(void)
{
    int window_count = 0;
    uint32_t window_list[1024] = {0};

    if (workspace_is_macos_sequoia() || workspace_is_macos_tahoe() || workspace_is_macos_goldengate()) {
        // NOTE(asmvik): Subscribe to all windows because of window_destroyed (and ordered) notifications
        table_for (struct window *window, g_window_manager.window, {
            window_list[window_count++] = window->id;
        })
    } else {
        // NOTE(asmvik): Subscribe to windows that have a feedback_border because of window_ordered notifications
        table_for (struct window_node *node, g_window_manager.insert_feedback, {
            window_list[window_count++] = node->window_order[0];
        })
    }

    SLSRequestNotificationsForWindows(g_connection, window_list, window_count);
}

static void window_did_receive_focus(struct window_manager *wm, struct mouse_state *ms, struct window *window)
{
    struct window *focused_window = window_manager_find_window(wm, wm->focused_window_id);
    if (focused_window && focused_window != window && window_space(focused_window->id) == window_space(window->id)) {
        window_manager_set_window_opacity(wm, focused_window, g_window_manager.normal_window_opacity);
    }

    window_manager_set_window_opacity(wm, window, wm->active_window_opacity);

    if (wm->focused_window_id != window->id) {
        if (ms->ffm_window_id != window->id) {
            window_manager_center_mouse(wm, window);
        }

        wm->last_window_id = wm->focused_window_id;
    }

    wm->focused_window_id = window->id;
    wm->focused_window_psn = window->application->psn;
    ms->ffm_window_id = 0;

    struct view *view = window_manager_find_managed_window(&g_window_manager, window);
    if (!view) return;

    struct window_node *node = view_find_window_node(view, window->id);
    if (node->window_count <= 1) return;

    for (int i = 0; i < node->window_count; ++i) {
        if (node->window_order[i] != window->id) continue;

        memmove(node->window_order + 1, node->window_order, sizeof(uint32_t) * i);
        node->window_order[0] = window->id;

        break;
    }
}

#include "window_focus_events.c"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#include "handlers/applications.c"
#include "handlers/windows.c"
#include "handlers/spaces.c"
#include "handlers/displays.c"
#include "handlers/mouse.c"
#include "handlers/mission_control.c"
#include "handlers/system.c"
#include "handlers/messages.c"
#pragma clang diagnostic pop

#include "event_loop_trace.c"

static void *event_loop_run(void *context)
{
    struct event event;
    struct event_loop *event_loop = context;

    while (event_loop->is_running) {
        NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

        while (event_queue_pop(&event_loop->queue, &event)) {
            profile_begin();

            uint64_t started = read_os_timer();

            switch (event.type) {
#define EVENT_TYPE_ENTRY(value) case value: EVENT_HANDLER_##value(event.context, event.param1); break;
                EVENT_TYPE_LIST
#undef EVENT_TYPE_ENTRY
            }

            event_signal_flush();
            ts_reset();
            event_loop_trace(event.type, started);

            profile_end_and_print();
        }

        [pool drain];
        dispatch_semaphore_wait(event_loop->semaphore, DISPATCH_TIME_FOREVER);
    }

    return NULL;
}

void event_loop_post(struct event_loop *event_loop, enum event_type type, void *context, int param1)
{
    struct event replaced;
    uint32_t capacity;

    // Consecutive mouse moves keep only the latest; its handler releases the
    // event it is given, and we release the one it replaced. The move it
    // replaced was still queued, so its wake-up is still due: one more would
    // only wake the event loop to an empty queue.
    struct event event = { .type = type, .param1 = param1, .context = context };
    enum event_queue_result result = event_queue_push(&event_loop->queue, event, type == MOUSE_MOVED, &replaced, &capacity);

    if (result == EVENT_QUEUE_MERGED) {
        CFRelease(replaced.context);
        return;
    } else if (result == EVENT_QUEUE_GREW) {
        warn("%s: the event loop is falling behind, its queue now holds %u events\n", __FUNCTION__, capacity);
    } else if (result == EVENT_QUEUE_FULL) {
        warn("%s: could not grow the event queue, event %d dropped\n", __FUNCTION__, type);
        return;
    }

    dispatch_semaphore_signal(event_loop->semaphore);
}

// The wake-ups go through a semaphore of this process alone. A named one
// lives in a namespace every local account shares: a process that opened the
// name first could take our wake-ups, or keep us from starting.
static bool event_loop_init(struct event_loop *event_loop)
{
    if (!event_queue_init(&event_loop->queue, EVENT_QUEUE_CAPACITY)) return false;

    event_loop->semaphore = dispatch_semaphore_create(0);
    return event_loop->semaphore != NULL;
}

bool event_loop_begin(struct event_loop *event_loop)
{
    if (!event_loop_init(event_loop)) return false;

    event_loop->is_running = true;
    pthread_create(&event_loop->thread, NULL, &event_loop_run, event_loop);

    return true;
}
