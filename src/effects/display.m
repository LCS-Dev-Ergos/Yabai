static float space_navigation_frame_interval(uint32_t display)
{
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display);
    double refresh = mode ? CGDisplayModeGetRefreshRate(mode) : 0.0;
    if (mode) CGDisplayModeRelease(mode);

    // Some variable-refresh and scaled modes report zero. This is only the
    // window-fade fallback cadence and the snapshot overlay's timer cadence.
    if (!isfinite(refresh) || refresh < 1.0) refresh = 60.0;
    return 1.0 / fmin(refresh, 240.0);
}

static bool space_navigation_reduce_motion(void)
{
    return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion];
}
