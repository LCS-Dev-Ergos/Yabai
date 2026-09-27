struct space_navigation_effect
{
    struct sa_window_opacity windows[SA_OPACITY_BATCH_MAX];
    uint32_t count;
    float interval;
};

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

    return result;
}
