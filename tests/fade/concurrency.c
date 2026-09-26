static void *send_requests(void *context)
{
    uint32_t first = (uintptr_t)context;

    for (int i = 0; i < 200; ++i) {
        uint32_t wid = first + i % 6;
        window_fade_set(wid, i % 2 ? 0.3f : 0.8f, 0.04f);
    }

    for (uint32_t wid = first; wid < first + 6; ++wid) {
        window_fade_set(wid, 0.1f, 0.05f);
        window_fade_set(wid, 0.9f, 0.0f);
    }

    return NULL;
}

static void test_concurrent_requests(void)
{
    // All following reads of state shared with the real worker use its lock.
    for (uint32_t wid = 1; wid <= 32; ++wid) alphas[wid] = 1.0f;

    window_fade_set(1, 0.2f, 0.1f);
    usleep(20000);
    window_fade_set(1, 0.7f, 0.03f);
    wait_idle();

    pthread_mutex_lock(&window_fade_lock);
    assert(alphas[1] == 0.7f);
    pthread_mutex_unlock(&window_fade_lock);

    // A system call longer than one frame must not hold the request lock
    // for the entire animation. The bound is deliberately much wider than a frame.
    pthread_mutex_lock(&window_fade_lock);
    alpha_delay = 12000;
    pthread_mutex_unlock(&window_fade_lock);

    window_fade_set(1, 0.2f, 3.0f);
    usleep(30000);
    double request_start = window_fade_now();
    window_fade_set(1, 0.7f, 0.0f);
    assert(window_fade_now() - request_start < 1.0);

    pthread_mutex_lock(&window_fade_lock);
    alpha_delay = 0;
    pthread_mutex_unlock(&window_fade_lock);

    pthread_t clients[4];
    for (uintptr_t i = 0; i < 4; ++i) {
        assert(pthread_create(&clients[i], NULL, send_requests, (void *)(1 + i * 6)) == 0);
    }

    for (int i = 0; i < 4; ++i) pthread_join(clients[i], NULL);
    wait_idle();

    pthread_mutex_lock(&window_fade_lock);
    for (int wid = 1; wid <= 24; ++wid) assert(alphas[wid] == 0.9f);
    assert(thread_calls == 2); // One failed creation, then one shared worker.
    int calls_at_rest = alpha_calls;
    pthread_mutex_unlock(&window_fade_lock);

    usleep(30000);

    pthread_mutex_lock(&window_fade_lock);
    assert(alpha_calls == calls_at_rest);
    pthread_mutex_unlock(&window_fade_lock);
}
