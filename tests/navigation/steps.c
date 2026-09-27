static bool run_step(uint64_t sid, bool crossfade, bool activate)
{
    struct space_navigation_step step = {
        .sid = sid,
        .crossfade = crossfade,
        .alpha = crossfade ? 1.0f : .95f,
        .duration = .2f,
        .activate = activate
    };

    return space_navigation_run_step(active_space, &step);
}

// A crossfade asks Dock for the whole switch, and falls back to switching
// without an effect; a step that does not activate only switches.
static void test_steps(void)
{
    reset();
    assert(run_step(2, true, true));
    assert(crossfade_calls == 1 && last_crossfade_duration == .2f);
    assert(switched_duration == .2f);
    assert(focus_calls == 0 && batch_calls == 0 && opacity_calls == 0);
    assert(window_focus_calls == 1 && focused_id == 1 && activated_id == 1 && noted_id == 1);

    // Dock refused: the Desktop still changes, without an effect.
    reset();
    crossfade_success = false;
    assert(run_step(2, true, true));
    assert(crossfade_calls == 1 && focus_calls == 1 && active_space == 2 && window_focus_calls == 1);
    assert(switched_duration == 0.0f);

    // Neither Dock path works: the navigation fails and activates nothing.
    reset();
    crossfade_success = false;
    focus_success = false;
    assert(!run_step(2, true, true));
    assert(window_focus_calls == 0 && activated_id == 0);

    // A destination already visible elsewhere, or fullscreen, switches
    // without a crossfade.
    reset();
    visible = true;
    assert(run_step(3, true, true));
    assert(crossfade_calls == 0 && focus_calls == 1);

    reset();
    fullscreen = true;
    assert(run_step(2, true, true));
    assert(crossfade_calls == 0 && focus_calls == 1);

    // Reduce Motion keeps the crossfade: its own Desktop transition is one.
    reset();
    reduce_motion = true;
    assert(run_step(2, true, true));
    assert(crossfade_calls == 1 && focus_calls == 0);

    // An intermediate step switches with its effect, but activates nothing
    // and does not ask WindowServer about a raise.
    reset();
    other_window_space = visible_space = 3;
    assert(run_step(2, true, false));
    assert(crossfade_calls == 1 && window_focus_calls == 0 && raise_calls == 0);
    assert(window_list_queries == 0 && activated_id == 0 && noted_id == 0 && display_calls == 0);
    assert(space_navigation_anchor.sid == 2);

    reset();
    assert(run_step(2, false, false));
    assert(batch_calls == 2 && opacity_calls == 4 && focus_calls == 1 && window_focus_calls == 0);
}
