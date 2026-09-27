static bool run_step(uint64_t sid, bool crossfade)
{
    struct space_navigation_step step = {
        .sid = sid,
        .crossfade = crossfade,
        .alpha = crossfade ? 1.0f : .95f,
        .duration = .2f
    };

    return space_navigation_run_step(active_space, &step);
}

// A crossfade asks Dock for the whole switch, and falls back to switching
// without an effect.
static void test_steps(void)
{
    reset();
    assert(run_step(2, true));
    assert(crossfade_calls == 1 && last_crossfade_duration == .2f);
    assert(focus_calls == 0 && batch_calls == 0 && opacity_calls == 0);
    assert(window_focus_calls == 1 && focused_id == 1 && noted_id == 1);

    // Dock refused: the Desktop still changes, without an effect.
    reset();
    crossfade_success = false;
    assert(run_step(2, true));
    assert(crossfade_calls == 1 && focus_calls == 1 && active_space == 2 && window_focus_calls == 1);

    // Neither Dock path works: the navigation fails and activates nothing.
    reset();
    crossfade_success = false;
    focus_success = false;
    assert(!run_step(2, true));
    assert(window_focus_calls == 0);

    // A destination already visible elsewhere, or fullscreen, switches
    // without a crossfade.
    reset();
    visible = true;
    assert(run_step(3, true));
    assert(crossfade_calls == 0 && focus_calls == 1);

    reset();
    fullscreen = true;
    assert(run_step(2, true));
    assert(crossfade_calls == 0 && focus_calls == 1);

    // Reduce Motion keeps the crossfade: its own Desktop transition is one.
    reset();
    reduce_motion = true;
    assert(run_step(2, true));
    assert(crossfade_calls == 1 && focus_calls == 0);
}
