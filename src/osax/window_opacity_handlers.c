static void do_window_opacity_focus(char *message)
{
    uint32_t wid;
    float alpha, duration;
    unpack(wid);
    unpack(alpha);
    unpack(duration);
    window_fade_focus(wid, alpha, duration);
}

static void do_window_opacity_batch(int sockfd, char *message)
{
    uint32_t display, count;
    uint8_t phase;
    float alpha, duration, interval;
    unpack(display);
    unpack(phase);
    unpack(alpha);
    unpack(duration);
    unpack(interval);
    unpack(count);

    if (!display || phase > SA_OPACITY_RESTORE || count > SA_OPACITY_BATCH_MAX) return;
    if (!isfinite(alpha) || alpha < 0.0f || alpha > 1.0f) return;
    if (!isfinite(duration) || duration < 0.0f) return;
    if (!isfinite(interval) || interval < 1.0f / 240.0f || interval > 1.0f) return;
    if (count > unpack_capacity(sizeof(struct sa_window_opacity))) return;

    struct sa_window_opacity windows[SA_OPACITY_BATCH_MAX];
    for (uint32_t i = 0; i < count; ++i) {
        unpack(windows[i].wid);
        unpack(windows[i].alpha);
        if (!windows[i].wid || !isfinite(windows[i].alpha)) return;
        if (windows[i].alpha < 0.0f || windows[i].alpha > 1.0f) return;

        for (uint32_t j = 0; j < i; ++j) {
            if (windows[j].wid == windows[i].wid) return;
        }
    }

    bool success = window_fade_batch(display, phase, alpha, duration, interval, windows, count);
    char result = success ? 'k' : 'e';
    payload_reply(sockfd, &result, 1);
}
