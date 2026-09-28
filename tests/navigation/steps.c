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

static enum space_navigation_result begin_step(uint64_t sid, bool crossfade, bool activate)
{
    struct space_navigation_step step = {
        .sid = sid,
        .crossfade = crossfade,
        .alpha = crossfade ? 1.0f : .95f,
        .duration = .2f,
        .activate = activate
    };

    return space_navigation_begin_step(active_space, &step);
}

// A queued crossfade returns while its capture is under way. The capture's
// event switches, starts the crossfade, activates and reports to the schedule.
static void test_captured_steps(void)
{
    reset();
    assert(begin_step(2, true, true) == SPACE_NAVIGATION_PENDING);
    assert(capture_calls == 1 && snapshot_prepares == 0 && focus_calls == 0);
    assert(window_focus_calls == 0 && completed_calls == 0);

    space_navigation_step_captured(capture_token);
    assert(present_calls == 1 && focus_calls == 1 && snapshot_starts == 1 && switched_duration == .2f);
    assert(window_focus_calls == 1 && focused_id == 1 && activated_id == 1 && noted_id == 1);
    assert(completed_calls == 1 && completed_success && space_navigation_anchor.sid == 2);

    // Only the first event for the step counts: the deadline after the
    // callback, or an older step's event, finds nothing.
    space_navigation_step_captured(capture_token);
    space_navigation_step_captured(capture_token - 1);
    assert(present_calls == 1 && focus_calls == 1 && completed_calls == 1);

    // No usable image: the step switches without the crossfade.
    reset();
    present_result = SPACE_SNAPSHOT_MISSING;
    assert(begin_step(2, true, true) == SPACE_NAVIGATION_PENDING);
    space_navigation_step_captured(capture_token);
    assert(focus_calls == 1 && snapshot_starts == 0 && switched_duration == 0.0f);
    assert(window_focus_calls == 1 && completed_calls == 1 && completed_success);

    // Cancelled while it was captured, by a click, Mission Control or a
    // display change: nothing switches, and the step fails.
    reset();
    present_result = SPACE_SNAPSHOT_CANCELLED;
    assert(begin_step(2, true, true) == SPACE_NAVIGATION_PENDING);
    int cancels = snapshot_cancels;
    space_navigation_step_captured(capture_token);
    assert(focus_calls == 0 && window_focus_calls == 0 && completed_calls == 1 && !completed_success);
    assert(snapshot_cancels == cancels); // Nothing of ours left to remove.

    // A click the handlers have not seen yet stops the step as well, and
    // removes the image it presented.
    reset();
    click_during_snapshot = true;
    assert(begin_step(2, true, true) == SPACE_NAVIGATION_PENDING);
    cancels = snapshot_cancels;
    space_navigation_step_captured(capture_token);
    assert(focus_calls == 0 && snapshot_cancels == cancels + 1);
    assert(completed_calls == 1 && !completed_success);

    // So does Mission Control, and a display animating, since the capture.
    reset();
    assert(begin_step(2, true, true) == SPACE_NAVIGATION_PENDING);
    mission_control = true;
    space_navigation_step_captured(capture_token);
    assert(focus_calls == 0 && completed_calls == 1 && !completed_success);

    reset();
    assert(begin_step(2, true, true) == SPACE_NAVIGATION_PENDING);
    animating = true;
    space_navigation_step_captured(capture_token);
    assert(focus_calls == 0 && completed_calls == 1 && !completed_success);

    // Another command abandons the step: its event switches nothing and
    // reports nothing, since the schedule has been emptied already.
    reset();
    assert(begin_step(2, true, true) == SPACE_NAVIGATION_PENDING);
    space_navigation_step_cancel();
    space_navigation_step_captured(capture_token);
    assert(present_calls == 0 && focus_calls == 0 && completed_calls == 0);

    // The window chosen before the capture can be gone after it: the step
    // still switches, and activates nothing.
    reset();
    assert(begin_step(2, true, true) == SPACE_NAVIGATION_PENDING);
    destroyed_id = 1;
    space_navigation_step_captured(capture_token);
    assert(focus_calls == 1 && window_focus_calls == 0 && activated_id == 0);
    assert(completed_calls == 1 && completed_success);

    // Dock fails after the image was presented: the crossfade is ended and
    // nothing is activated.
    reset();
    focus_success = false;
    assert(begin_step(2, true, true) == SPACE_NAVIGATION_PENDING);
    space_navigation_step_captured(capture_token);
    assert(snapshot_starts == 1 && window_focus_calls == 0 && completed_calls == 1 && !completed_success);

    // No capture could start: the step switches at once, without crossfade.
    reset();
    capture_starts = false;
    assert(begin_step(2, true, true) == SPACE_NAVIGATION_SWITCHED);
    assert(capture_calls == 1 && focus_calls == 1 && snapshot_starts == 0 && window_focus_calls == 1);
    assert(completed_calls == 0);

    // Steps without a capture run to their end at once.
    reset();
    assert(begin_step(2, false, true) == SPACE_NAVIGATION_SWITCHED);
    assert(capture_calls == 0 && batch_calls == 2 && focus_calls == 1 && window_focus_calls == 1);

    reset();
    visible = true;
    assert(begin_step(3, true, true) == SPACE_NAVIGATION_SWITCHED);
    assert(capture_calls == 0 && focus_calls == 1);

    reset();
    mission_control = true;
    assert(begin_step(2, true, true) == SPACE_NAVIGATION_FAILED);
    assert(capture_calls == 0 && focus_calls == 0);
    assert(completed_calls == 0);
}

// A crossfade captures the source, asks Dock for an ordinary switch, then
// fades only yabai's snapshot; the daemon has no request that changes Space
// alpha or levels.
static void test_steps(void)
{
    reset();
    assert(run_step(2, true, true));
    assert(snapshot_prepares == 1 && snapshot_starts == 1);
    assert(last_crossfade_duration == .2f);
    assert(switched_duration == .2f);
    assert(focus_calls == 1 && batch_calls == 0 && opacity_calls == 0);
    assert(window_focus_calls == 1 && focused_id == 1 && activated_id == 1 && noted_id == 1);

    // Capture refused or timed out: still switch without changing real alpha.
    reset();
    crossfade_success = false;
    assert(run_step(2, true, true));
    assert(snapshot_prepares == 1 && snapshot_starts == 0);
    assert(focus_calls == 1 && active_space == 2 && window_focus_calls == 1);
    assert(switched_duration == 0.0f);

    // Neither Dock path works: the navigation fails and activates nothing.
    reset();
    crossfade_success = false;
    focus_success = false;
    assert(!run_step(2, true, true));
    assert(window_focus_calls == 0 && activated_id == 0);

    // The image was prepared but Dock failed: restore the screen before any focus.
    reset();
    focus_success = false;
    assert(!run_step(2, true, true));
    assert(snapshot_prepares == 1 && snapshot_starts == 1 && window_focus_calls == 0);

    reset();
    click_during_snapshot = true;
    assert(!run_step(2, true, true));
    assert(snapshot_prepares == 1 && snapshot_starts == 0 && focus_calls == 0);
    assert(snapshot_cancels == 2); // New pointer input wins before the switch.

    // A destination already visible elsewhere, or fullscreen, switches
    // without a crossfade.
    reset();
    visible = true;
    assert(run_step(3, true, true));
    assert(snapshot_prepares == 0 && focus_calls == 1);

    reset();
    fullscreen = true;
    assert(run_step(2, true, true));
    assert(snapshot_prepares == 0 && focus_calls == 1);

    // Reduce Motion keeps the crossfade: its own Desktop transition is one.
    reset();
    reduce_motion = true;
    assert(run_step(2, true, true));
    assert(snapshot_prepares == 1 && snapshot_starts == 1 && focus_calls == 1);

    // An intermediate step switches with its effect, but activates nothing
    // and does not ask WindowServer about a raise.
    reset();
    other_window_space = visible_space = 3;
    assert(run_step(2, true, false));
    assert(snapshot_prepares == 1 && snapshot_starts == 1 && window_focus_calls == 0 && raise_calls == 0);
    assert(window_list_queries == 0 && activated_id == 0 && noted_id == 0 && display_calls == 0);
    assert(space_navigation_anchor.sid == 2);

    reset();
    assert(run_step(2, false, false));
    assert(batch_calls == 2 && opacity_calls == 4 && focus_calls == 1 && window_focus_calls == 0);

    // The last step of a burst can find its Desktop current already. When the
    // steps before it left activation to it, it activates that Desktop's
    // window, and switches nothing.
    reset();
    struct space_navigation_step settle = {
        .sid = active_space,
        .crossfade = true,
        .alpha = 1.0f,
        .duration = .2f,
        .activate = true,
        .settle = true
    };
    assert(space_navigation_run_step(active_space, &settle));
    assert(focus_calls == 0 && snapshot_prepares == 0 && snapshot_cancels == 0 && opacity_calls == 0);
    assert(window_focus_calls == 1 && focused_id == 1 && activated_id == 1 && noted_id == 1);

    // Nothing to do when that window has focus, nor for a plain request for
    // the current Desktop.
    reset();
    g_window_manager.focused_window_id = 1;
    assert(space_navigation_run_step(active_space, &settle));
    assert(window_focus_calls == 0 && activated_id == 0);

    reset();
    settle.settle = false;
    assert(space_navigation_run_step(active_space, &settle));
    assert(window_focus_calls == 0 && focus_cancels == 0 && activated_id == 0);

    // Mission Control fails the step, as it fails a switch.
    reset();
    settle.settle = true;
    mission_control = true;
    assert(!space_navigation_run_step(active_space, &settle));
    assert(window_focus_calls == 0);
}
