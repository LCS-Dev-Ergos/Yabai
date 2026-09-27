static void test_navigation_ownership(void)
{
    struct window_fade_context *fade = add_fade(1);
    fade->display = 1;
    fade->started = window_fade_now();
    double started = fade->started;
    double end = started + fade->duration;
    int calls = alpha_calls;

    // The WINDOW_FOCUSED policy confirmation must not jump to the endpoint.
    window_fade_focus(1, 1.0f, 0.0f);
    window_fade_focus(1, 1.0f, 0.2f);
    assert(window_fades == fade && fade->started == started);
    assert(alpha_calls == calls && alphas[1] == 0.2f);

    window_fade_tick(started + 0.05);
    assert(fabsf(alphas[1] - 0.6f) < 0.00001f); // Bounded smoothstep midpoint.
    float current = alphas[1];

    window_fade_focus(1, 0.975f, 0.0f);
    assert(fade->from == current && fade->to == 0.975f);
    assert(fabs(fade->started + fade->duration - end) < 0.00001);
    window_fade_tick(end + 0.001);
    assert(!window_fades && alphas[1] == 0.975f);

    fade = add_fade(1);
    fade->display = 1;
    window_fade_set(1, 1.0f, 0.0f); // Explicit same-target command still cancels.
    assert(!window_fades && alphas[1] == 1.0f);
}

static void test_navigation_cadence(void)
{
    struct window_fade_context *fade = add_fade(1);
    fade->display = 1;
    fade->interval = 1.0 / 60.0;
    fade->next_frame = 10.0 + fade->interval;
    int calls = alpha_calls;

    window_fade_tick(10.008);
    assert(alpha_calls == calls);
    window_fade_tick(10.017);
    assert(alpha_calls == calls + 1);
    window_fade_tick(10.025);
    assert(alpha_calls == calls + 1);

    // A delivered display callback advances once; extra wakes do no work.
    fade->display_paced = true;
    fade->frame_ready = true;
    window_fade_tick(10.030);
    window_fade_tick(10.035);
    assert(alpha_calls == calls + 2);

    // No further callback (sleep/stall): the deadline still restores alpha.
    window_fade_tick(10.101);
    assert(!window_fades && alphas[1] == 1.0f);
}

static void test_navigation_batch(void)
{
    struct sa_window_opacity windows[] = { {1, .9f}, {2, .9f} };
    int reads = alpha_reads;
    assert(window_fade_batch(1, SA_OPACITY_PREPARE, .9f, 0, 1.0f / 60, windows, 2));
    windows[0].alpha = 1.0f;
    windows[1].alpha = .975f;
    assert(window_fade_batch(1, SA_OPACITY_START, .9f, 1, 1.0f / 60, windows, 2));

    pthread_mutex_lock(&window_fade_lock);
    assert(window_fades && window_fades->next);
    assert(window_fades->started == window_fades->next->started);
    assert(alpha_reads == reads); // The already-applied start needs no round trip.
    pthread_mutex_unlock(&window_fade_lock);

    // Another display must not cancel this group. A repeat on this one must.
    assert(window_fade_batch(2, SA_OPACITY_PREPARE, .9f, 0, 1.0f / 60, NULL, 0));
    pthread_mutex_lock(&window_fade_lock);
    assert(window_fades && window_fades->next);
    pthread_mutex_unlock(&window_fade_lock);

    window_fade_focus(2, .8f, 0);
    assert(window_fade_batch(1, SA_OPACITY_PREPARE, .9f, 0, 1.0f / 60, NULL, 0));
    pthread_mutex_lock(&window_fade_lock);
    assert(!window_fades && alphas[1] == 1.0f && alphas[2] == .8f);
    pthread_mutex_unlock(&window_fade_lock);

    fail_alloc = true;
    assert(!window_fade_batch(1, SA_OPACITY_START, .9f, .15f, 1.0f / 60, windows, 2));
    fail_alloc = false;
    pthread_mutex_lock(&window_fade_lock);
    assert(!window_fades && alphas[1] == 1.0f && alphas[2] == .975f);
    pthread_mutex_unlock(&window_fade_lock);
}
