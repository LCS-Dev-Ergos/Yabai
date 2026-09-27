// All helpers here serialize with the worker. Explicit opacity commands keep
// their original cancellation behavior; only focus policy preserves an effect.
static void window_fade_focus(uint32_t wid, float alpha, float duration)
{
    if (!wid || !isfinite(alpha) || alpha < 0.0f || alpha > 1.0f) return;
    if (!isfinite(duration) || duration < 0.0f) return;

    pthread_mutex_lock(&window_fade_lock);
    struct window_fade_context *fade = window_fades;
    while (fade && fade->wid != wid) fade = fade->next;

    if (fade && fade->display) {
        if (fade->to != alpha) {
            double now = window_fade_now();
            double remaining = fade->started + fade->duration - now;
            fade->from = fade->current;
            fade->to = alpha;
            fade->started = now;
            fade->duration = fmax(remaining, 0.000001);
        }

        pthread_mutex_unlock(&window_fade_lock);
        return;
    }

    pthread_mutex_unlock(&window_fade_lock);
    window_fade_set(wid, alpha, duration);
}

static void window_fade_cancel_display(uint32_t display)
{
    struct window_fade_context **slot = &window_fades;

    while (*slot) {
        if ((*slot)->display == display) {
            SLSSetWindowAlpha(SLSMainConnectionID(), (*slot)->wid, (*slot)->to);
            window_fade_remove(slot);
        } else {
            slot = &(*slot)->next;
        }
    }
}

static bool window_fade_batch(uint32_t display, uint8_t phase, float alpha, float duration,
                              float interval, struct sa_window_opacity *windows, uint32_t count)
{
    pthread_mutex_lock(&window_fade_lock);
    if (phase == SA_OPACITY_PREPARE) window_fade_cancel_display(display);

    bool animate = phase == SA_OPACITY_START && duration > 0.0f && count;
    bool worker = !animate || window_fade_start_worker();
    bool success = worker;
    double started = window_fade_now();

    for (uint32_t i = 0; i < count; ++i) {
        struct window_fade_context **slot = &window_fades;
        while (*slot && (*slot)->wid != windows[i].wid) slot = &(*slot)->next;
        window_fade_remove(slot);

        struct window_fade_context *fade = animate && worker ? calloc(1, sizeof(*fade)) : NULL;
        if (fade) {
            fade->wid = windows[i].wid;
            fade->from = fade->current = alpha;
            fade->to = windows[i].alpha;
            fade->started = started;
            fade->duration = duration;
            fade->display = display;
            fade->interval = interval;
            fade->next_frame = started + interval;
            fade->next = *slot;
            *slot = fade;
        } else {
            if (animate) success = false;
            if (SLSSetWindowAlpha(SLSMainConnectionID(), windows[i].wid, windows[i].alpha) != 0) success = false;
        }
    }

    if (animate && count && worker) window_fade_display_start(display);
    window_fade_wake_worker();
    pthread_mutex_unlock(&window_fade_lock);
    return success;
}
