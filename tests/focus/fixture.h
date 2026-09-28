#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum { WINDOW_MINIMIZE = 1, SIGNAL_WINDOW_FOCUSED = 1 };

struct application
{
    int psn;
};

struct window
{
    uint32_t id;
    uint32_t *id_ptr;
    struct application *application;
    unsigned flags;
};

static struct application app = {1}, other_app = {2};
static struct window windows[2];
static struct
{
    uint32_t focused_window_id, last_window_id;
    int focused_window_psn;
    float normal_window_opacity;
} g_window_manager;
static struct { uint32_t ffm_window_id; } g_mouse_state;

static volatile bool __pending_window_focus;
static volatile uint32_t __pending_window_focus_id;
static bool frontmost, visible;
static uint32_t ax_id, lost_id;
static int ax_calls, focus_calls, signals, opacity_calls;

static struct window *window_manager_find_window(void *wm, uint32_t id)
{
    for (int i = 0; i < 2; ++i) {
        if (windows[i].id == id) return &windows[i];
    }

    return NULL;
}

static uint32_t application_focused_window(struct application *application)
{
    ++ax_calls;
    return ax_id;
}

static bool application_is_frontmost(struct application *application)
{
    return frontmost;
}

static bool window_check_flag(struct window *window, unsigned flag)
{
    return window->flags & flag;
}

static uint64_t window_space(uint32_t id)
{
    return 1;
}

static bool space_is_visible(uint64_t sid)
{
    return visible;
}

static void window_manager_set_window_opacity(void *wm, struct window *window, float alpha)
{
    ++opacity_calls;
}

static void window_manager_add_lost_focused_event(void *wm, uint32_t id)
{
    lost_id = id;
}

static void window_did_receive_focus(void *wm, void *ms, struct window *window)
{
    ++focus_calls;
    g_window_manager.focused_window_id = window->id;
}

static void event_signal_push(int type, struct window *window)
{
    assert(type == SIGNAL_WINDOW_FOCUSED);
    ++signals;
}

#include "../../src/events/window_focus_events.c"

static void reset(void)
{
    for (int i = 0; i < 2; ++i) {
        windows[i] = (struct window) { .id = i+1, .application = &app };
        windows[i].id_ptr = &windows[i].id;
    }

    ax_id = 2;
    lost_id = 0;
    ax_calls = focus_calls = signals = opacity_calls = 0;
    frontmost = visible = true;
    __pending_window_focus = true;
    __pending_window_focus_id = 2;
    g_window_manager.focused_window_id = 1;
    g_window_manager.last_window_id = 0;
    g_mouse_state.ffm_window_id = 2;
}
