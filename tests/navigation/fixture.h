#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
};

struct window
{
    uint32_t id;
    float opacity;
    unsigned flags;
    struct application *application;
    void *ref;
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
static bool visible, fullscreen, mission_control, animating, focus_success;
static int focus_calls, opacity_calls, window_focus_calls, raise_calls, move_calls, display_calls;
static int opacity_fail_at;
static uint32_t focused_id;
static double seconds_since_click;
static uint32_t ids[] = { 1, 2 };

static struct
{
    uint32_t id;
    float alpha;
    float duration;
} effects[16];

#include "stubs.h"
#include "../../src/space_navigation.c"

static void reset(void)
{
    memset(&app, 0, sizeof(app));

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
    visible = fullscreen = mission_control = animating = false;
    focus_success = true;

    focus_calls = opacity_calls = window_focus_calls = raise_calls = move_calls = display_calls = 0;
    focused_id = 0;
    opacity_fail_at = 0;
}
