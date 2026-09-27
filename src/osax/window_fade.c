#include <math.h>
#include <stdbool.h>
#include <time.h>

struct window_fade_context
{
    struct window_fade_context *next;
    uint32_t wid;
    float from;
    float to;
    float current;
    double started;
    double duration;
    uint32_t display;
    double interval;
    double next_frame;
    bool display_paced;
    bool frame_ready;
};

static pthread_mutex_t window_fade_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t window_fade_cond = PTHREAD_COND_INITIALIZER;
static struct window_fade_context *window_fades;
static bool window_fade_worker_started;

static void window_fade_display_stop(uint32_t display);

static double window_fade_now(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return time.tv_sec + time.tv_nsec / 1e9;
}

static float window_fade_alpha(struct window_fade_context *fade, double now)
{
    double t = (now - fade->started) / fade->duration;
    if (t <= 0.0) return fade->from;
    if (t >= 1.0) return fade->to;

    double remaining = 1.0 - t;
    double eased = fade->display ? t * t * (3.0 - 2.0 * t)
                                : 1.0 - remaining * remaining * remaining;
    return fade->from + (fade->to - fade->from) * eased;
}

static void window_fade_remove(struct window_fade_context **slot)
{
    struct window_fade_context *fade = *slot;
    if (!fade) return;

    *slot = fade->next;
    if (fade->display) {
        struct window_fade_context *other = window_fades;
        while (other && other->display != fade->display) other = other->next;
        if (!other) window_fade_display_stop(fade->display);
    }
    free(fade);
}

// The worker and all requests hold window_fade_lock while applying alpha.
// An immediate request therefore cannot be overwritten by an older frame.
static void window_fade_tick(double now)
{
    struct window_fade_context **slot = &window_fades;

    while (*slot) {
        struct window_fade_context *fade = *slot;
        double end = fade->started + fade->duration;
        if (!fade->frame_ready && now < fade->next_frame && now < end) {
            slot = &fade->next;
            continue;
        }

        fade->frame_ready = false;
        fade->next_frame = now + (fade->display_paced ? 0.05 : fade->interval);
        float alpha = window_fade_alpha(fade, now);
        bool failed = false;

        if (alpha != fade->current) {
            failed = SLSSetWindowAlpha(SLSMainConnectionID(), fade->wid, alpha) != 0;
            fade->current = alpha;
        }

        if (failed || now >= fade->started + fade->duration) {
            window_fade_remove(slot);
        } else {
            slot = &fade->next;
        }
    }
}

#include "window_fade_worker.c"

static void window_fade_set(uint32_t wid, float alpha, float duration)
{
    if (!wid || !isfinite(alpha) || alpha < 0.0f || alpha > 1.0f) return;
    if (!isfinite(duration) || duration < 0.0f) return;

    pthread_mutex_lock(&window_fade_lock);
    struct window_fade_context **slot = &window_fades;
    while (*slot && (*slot)->wid != wid) slot = &(*slot)->next;

    if (duration == 0.0f) {
        window_fade_remove(slot);
        SLSSetWindowAlpha(SLSMainConnectionID(), wid, alpha);
        pthread_mutex_unlock(&window_fade_lock);
        return;
    }

    float start;
    if (SLSGetWindowAlpha(SLSMainConnectionID(), wid, &start) != 0 || start == alpha) {
        window_fade_remove(slot);
        pthread_mutex_unlock(&window_fade_lock);
        return;
    }

    if (!window_fade_start_worker()) {
        window_fade_remove(slot);
        SLSSetWindowAlpha(SLSMainConnectionID(), wid, alpha);
        pthread_mutex_unlock(&window_fade_lock);
        return;
    }

    if (!*slot) *slot = calloc(1, sizeof(struct window_fade_context));
    if (!*slot) {
        SLSSetWindowAlpha(SLSMainConnectionID(), wid, alpha);
        pthread_mutex_unlock(&window_fade_lock);
        return;
    }

    struct window_fade_context *fade = *slot;
    fade->wid = wid;
    fade->from = start;
    fade->current = start;
    fade->to = alpha;
    fade->duration = duration;
    fade->started = window_fade_now();
    fade->display = 0;
    fade->display_paced = false;
    fade->frame_ready = false;
    fade->interval = 1.0 / 120.0;
    fade->next_frame = fade->started;

    pthread_cond_signal(&window_fade_cond);
    pthread_mutex_unlock(&window_fade_lock);
}
