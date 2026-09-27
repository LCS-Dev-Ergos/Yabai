static void *window_fade_worker(void *unused)
{
    pthread_mutex_lock(&window_fade_lock);

    for (;;) {
        double now = window_fade_now();
        window_fade_take_frames();
        window_fade_tick(now);

        dispatch_time_t until = DISPATCH_TIME_FOREVER;

        // Display callbacks wake navigation fades; their watchdog also
        // completes a fade if the screen sleeps or its run loop stalls.
        if (window_fades) {
            double deadline = now + 1.0;
            for (struct window_fade_context *fade = window_fades; fade; fade = fade->next) {
                double end = fade->started + fade->duration;
                if (end < deadline) deadline = end;
                if (fade->next_frame < deadline) deadline = fade->next_frame;
            }

            double remaining = deadline - window_fade_now();
            // Even an overdue frame must sleep briefly: unlock/relock alone
            // can starve pending requests on an unfair pthread mutex.
            if (remaining < 0.001) remaining = 0.001;

            until = dispatch_time(DISPATCH_TIME_NOW, (int64_t)(remaining * NSEC_PER_SEC));
        }

        pthread_mutex_unlock(&window_fade_lock);
        dispatch_semaphore_wait(window_fade_wake, until);
        pthread_mutex_lock(&window_fade_lock);
    }

    return NULL;
}

static bool window_fade_start_worker(void)
{
    if (window_fade_worker_started) return true;

    if (!window_fade_wake) window_fade_wake = dispatch_semaphore_create(0);
    if (!window_fade_wake) return false;

    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0) return false;

    int result = pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);

    // Frames are due at display rate; Dock's default QoS lets UI work delay them.
    if (result == 0) result = pthread_attr_set_qos_class_np(&attributes, QOS_CLASS_USER_INTERACTIVE, 0);

    if (result == 0) {
        pthread_t thread;
        result = pthread_create(&thread, &attributes, window_fade_worker, NULL);
    }

    pthread_attr_destroy(&attributes);
    window_fade_worker_started = result == 0;
    return window_fade_worker_started;
}
