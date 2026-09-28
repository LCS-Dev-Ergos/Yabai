// When the last effect started on each display may still run. Only navigation
// starts display effects, so a preparation with nothing to dim, which would
// only cancel them, can skip its Dock round trip once they have ended. The
// slack covers the Dock worker's 50 ms watchdog and scheduling delays.
#define SPACE_NAVIGATION_EFFECT_DISPLAYS 16
#define SPACE_NAVIGATION_EFFECT_SLACK_NS 100000000ULL

static struct
{
    uint32_t display;
    uint64_t until;
} space_navigation_effect_until[SPACE_NAVIGATION_EFFECT_DISPLAYS];

static bool space_navigation_effect_pending(uint32_t display, uint64_t now)
{
    for (int i = 0; i < SPACE_NAVIGATION_EFFECT_DISPLAYS; ++i) {
        if (space_navigation_effect_until[i].display == display) {
            return now < space_navigation_effect_until[i].until;
        }
    }

    return false;
}

static void space_navigation_effect_started(uint32_t display, uint64_t until)
{
    int slot = 0;

    for (int i = 0; i < SPACE_NAVIGATION_EFFECT_DISPLAYS; ++i) {
        if (space_navigation_effect_until[i].display == display) {
            slot = i;
            break;
        }

        if (space_navigation_effect_until[i].until < space_navigation_effect_until[slot].until) {
            slot = i;
        }
    }

    space_navigation_effect_until[slot].display = display;
    space_navigation_effect_until[slot].until = until;
}

static bool space_navigation_prepare_effect(struct space_navigation_effect *effect, uint32_t display,
                                            uint32_t *ids, int count, uint32_t focus_id, float alpha, bool fade)
{
    effect->interval = fade ? space_navigation_frame_interval(display) : 1.0f / 60.0f;
    effect->count = 0;

    if (fade) {
        for (int i = 0; i < count; ++i) {
            struct window *window = window_manager_find_window(&g_window_manager, ids[i]);
            if (!space_navigation_window(window) || alpha >= space_navigation_opacity(window, focus_id)) continue;

            // Never partly animate an unusually large Desktop.
            if (effect->count == SA_OPACITY_BATCH_MAX) {
                effect->count = 0;
                break;
            }

            effect->windows[effect->count++] = (struct sa_window_opacity) { window->id, alpha };
        }
    }

    if (!effect->count && !space_navigation_effect_pending(display, read_os_timer())) return true;

    return scripting_addition_set_opacity_batch(display, SA_OPACITY_PREPARE, alpha, 0.0f,
                                                effect->interval, effect->windows, effect->count);
}

static bool space_navigation_start_effect(struct space_navigation_effect *effect, uint32_t display,
                                          uint32_t focus_id, float alpha, float duration, bool success)
{
    if (!effect->count) return true;

    for (uint32_t i = 0; i < effect->count; ++i) {
        struct window *window = window_manager_find_window(&g_window_manager, effect->windows[i].wid);
        effect->windows[i].alpha = space_navigation_opacity(window, focus_id);
    }

    bool result = scripting_addition_set_opacity_batch(display, success ? SA_OPACITY_START : SA_OPACITY_RESTORE,
                                                       alpha, success ? duration : 0.0f, effect->interval,
                                                       effect->windows, effect->count);
    if (!result && success) {
        scripting_addition_set_opacity_batch(display, SA_OPACITY_RESTORE, alpha, 0.0f,
                                             effect->interval, effect->windows, effect->count);
    }

    if (result && success) {
        uint64_t length = (uint64_t) (duration * 1e9) + SPACE_NAVIGATION_EFFECT_SLACK_NS;
        space_navigation_effect_started(display, read_os_timer() + length);
    }

    return result;
}
