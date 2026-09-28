bool scripting_addition_set_focus_opacity(uint32_t wid, float opacity, float duration)
{
    sa_payload_init();
    pack(wid);
    pack(opacity);
    pack(duration);
    return sa_payload_send(SA_OPCODE_WINDOW_OPACITY_FOCUS);
}

bool scripting_addition_set_opacity_batch(uint32_t display, uint8_t phase, float alpha, float duration,
                                         float interval, struct sa_window_opacity *windows, uint32_t count)
{
    if (count > SA_OPACITY_BATCH_MAX) return false;

    sa_payload_init();
    pack(display);
    pack(phase);
    pack(alpha);
    pack(duration);
    pack(interval);
    pack(count);

    for (uint32_t i = 0; i < count; ++i) {
        pack(windows[i].wid);
        pack(windows[i].alpha);
    }

    return sa_payload_send(SA_OPCODE_WINDOW_OPACITY_BATCH);
}
