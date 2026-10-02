#include <CoreFoundation/CoreFoundation.h>
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../src/osax/common.h"
#include "../../src/effects/snapshot.h"

enum
{
    WINDOW_MINIMIZE = 1,
    WINDOW_STICKY = 2,
    WINDOW_FULLSCREEN = 4
};

struct application
{
    bool is_hidden;
    int psn;
    int connection;
};

struct window
{
    uint32_t id;
    float opacity;
    unsigned flags;
    struct application *application;
    void *ref;
};

struct view
{
    uint64_t sid;
};

static struct application app;
static struct window windows[2];

static struct
{
    bool enable_window_opacity;
    float active_window_opacity;
    float normal_window_opacity;
    uint32_t focused_window_id;
} g_window_manager;

static int g_space_manager;
static uint64_t active_space, timestamp, visible_space, other_window_space;
static bool visible, fullscreen, mission_control, animating, focus_success, single_display, single_window;
static bool other_window_tiled;
static int focus_calls, opacity_calls, window_focus_calls, raise_calls, move_calls, display_calls;
static int click_raise_calls;
static bool crossfade_success;
static float last_crossfade_duration;
static int snapshot_prepares, snapshot_starts, snapshot_cancels, veil_prepares;
static bool veil_success;
static bool click_during_snapshot;
static bool capture_starts;
static int capture_calls, present_calls, completed_calls, capture_token;
static int discard_calls, pending_token;
static uint64_t move_sid;
static enum space_snapshot_result present_result;
static bool completed_success;
static uint32_t destroyed_id;
static uint32_t activated_id;
static int opacity_fail_at;
static bool expect_fade_started;
static bool reduce_motion;
static bool memory_pressure;
static int batch_calls;
static int window_list_queries, focus_cancels;
static bool defer_activation;
static int defer_queries;
static int switches_at_query;
static uint32_t focused_id, noted_id;
static double seconds_since_click;
static uint32_t ids[] = { 1, 2 };

static struct
{
    uint32_t id;
    float alpha;
    float duration;
} effects[16];

#include "../../src/navigation/topology.h"
#include "../../src/navigation/step.h"
#ifdef NAVIGATION_REAL_SCHEDULE
#include "../../src/navigation/schedule.h"
static enum space_navigation_result space_navigation_execute(struct space_navigation_request *request, int steps,
                                                             bool activate, bool settle, float duration);
static void space_navigation_schedule_after(uint64_t delay_ns);
#endif
#include "../../src/effects/window_fade.h"
#include "stubs.h"
#include "../../src/effects/window_fade.c"
#include "../../src/navigation/step.c"
#ifdef NAVIGATION_REAL_SCHEDULE
#include "../../src/navigation/schedule.c"
#endif

// A single activating step with the window fade, as before the schedule.
static bool space_navigation_run(uint64_t current, uint64_t sid, bool move, float alpha, float duration)
{
    struct space_navigation_step step = {
        .sid = sid,
        .move = move,
        .alpha = alpha,
        .duration = duration,
        .activate = true
    };

    return space_navigation_run_step(current, &step);
}

static void reset(void)
{
#ifdef NAVIGATION_REAL_SCHEDULE
    memset(&g_space_navigation_schedule, 0, sizeof(g_space_navigation_schedule));
    g_space_navigation_schedule.pacing = true;
#endif
    memset(&app, 0, sizeof(app));
    app.connection = 7;

    windows[0] = (struct window) { .id = 1, .application = &app };
    windows[1] = (struct window) { .id = 2, .application = &app };

    g_window_manager.enable_window_opacity = true;
    g_window_manager.active_window_opacity = 1.0f;
    g_window_manager.normal_window_opacity = 0.975f;
    g_window_manager.focused_window_id = 9;

    active_space = 1;
    timestamp = 1000000000;
    visible_space = other_window_space = 0;
    seconds_since_click = 1000.0;
    space_navigation_last_time = 0;
    space_navigation_forget();
    memset(space_navigation_effect_until, 0, sizeof(space_navigation_effect_until));
    visible = fullscreen = mission_control = animating = single_display = single_window = false;
    other_window_tiled = false;
    focus_success = true;

    focus_calls = opacity_calls = window_focus_calls = raise_calls = move_calls = display_calls = 0;
    click_raise_calls = 0;
    crossfade_success = true;
    last_crossfade_duration = 0.0f;
    snapshot_prepares = snapshot_starts = snapshot_cancels = veil_prepares = 0;
    veil_success = true;
    click_during_snapshot = false;
    capture_starts = true;
    capture_calls = present_calls = completed_calls = capture_token = 0;
    discard_calls = pending_token = 0;
    move_sid = 0;
    present_result = SPACE_SNAPSHOT_READY;
    completed_success = false;
    destroyed_id = 0;
    space_navigation_step_cancel();
    activated_id = 0;
    focused_id = noted_id = 0;
    opacity_fail_at = 0;
    expect_fade_started = false;
    reduce_motion = false;
    memory_pressure = false;
    batch_calls = 0;
    window_list_queries = focus_cancels = 0;
    switches_at_query = -1;
    defer_activation = false;
    defer_queries = 0;
}
