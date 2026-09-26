static void *window_fade_worker(void *unused)
{
    pthread_mutex_lock(&window_fade_lock);

    for (;;) {
        while (!window_fades) pthread_cond_wait(&window_fade_cond, &window_fade_lock);

        double now = window_fade_now();
        window_fade_tick(now);

        // One cadence for all windows, capped at 120 Hz. Late frames use
        // current time rather than replaying missed steps or adding sleeps.
        double deadline = now + 1.0 / 120.0;
        for (struct window_fade_context *fade = window_fades; fade; fade = fade->next) {
            double end = fade->started + fade->duration;
            if (end < deadline) deadline = end;
        }

        while (window_fades) {
            double remaining = deadline - window_fade_now();
            // Even an overdue frame must sleep briefly: unlock/relock alone
            // can starve pending requests on an unfair pthread mutex.
            if (remaining < 0.001) remaining = 0.001;

            struct timespec wait = {
                .tv_sec = (time_t)remaining,
                .tv_nsec = (long)((remaining - (time_t)remaining) * 1e9)
            };

            // A new request may wake us early; keep the shared frame deadline.
            pthread_cond_timedwait_relative_np(&window_fade_cond, &window_fade_lock, &wait);
            if (window_fade_now() >= deadline) break;
        }
    }

    return NULL;
}

static bool window_fade_start_worker(void)
{
    if (window_fade_worker_started) return true;

    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0) return false;

    int result = pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
    if (result == 0) {
        pthread_t thread;
        result = pthread_create(&thread, &attributes, window_fade_worker, NULL);
    }

    pthread_attr_destroy(&attributes);
    window_fade_worker_started = result == 0;
    return window_fade_worker_started;
}
