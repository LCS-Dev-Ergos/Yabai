#include <assert.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static float alphas[64];
static int alpha_calls;
static int thread_calls;
static useconds_t alpha_delay;
static bool fail_get, fail_set, fail_thread;

static int SLSMainConnectionID(void)
{
    return 0;
}

static int SLSGetWindowAlpha(int cid, uint32_t wid, float *alpha)
{
    assert(wid < 64);
    *alpha = alphas[wid];
    return fail_get;
}

static int SLSSetWindowAlpha(int cid, uint32_t wid, float alpha)
{
    assert(wid < 64);
    assert(isfinite(alpha) && alpha >= 0.0f && alpha <= 1.0f);

    ++alpha_calls;
    if (alpha_delay) usleep(alpha_delay);
    if (!fail_set) alphas[wid] = alpha;
    return fail_set;
}

static int create_worker(pthread_t *thread, const pthread_attr_t *attributes,
                         void *(*entry)(void *), void *context)
{
    ++thread_calls;
    return fail_thread ? EAGAIN : pthread_create(thread, attributes, entry, context);
}

#define pthread_create create_worker
#include "../../src/osax/window_fade.c"
#undef pthread_create

static struct window_fade_context *add_fade(uint32_t wid)
{
    struct window_fade_context *fade = calloc(1, sizeof(*fade));
    assert(fade);

    fade->wid = wid;
    fade->from = fade->current = 0.2f;
    fade->to = 1.0f;
    fade->started = 10.0;
    fade->duration = 0.1;
    fade->next = window_fades;
    window_fades = fade;
    alphas[wid] = fade->from;
    return fade;
}

static void wait_idle(void)
{
    double deadline = window_fade_now() + 5.0;

    for (;;) {
        pthread_mutex_lock(&window_fade_lock);
        bool idle = window_fades == NULL;
        pthread_mutex_unlock(&window_fade_lock);

        if (idle) return;
        assert(window_fade_now() < deadline);
        usleep(1000);
    }
}
