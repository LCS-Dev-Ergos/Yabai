#include "fixture.h"
#include "raise.c"
#include "steps.c"

int main(void)
{
    test_raise_queries();
    reset();

    expect_fade_started = true;
    assert(space_navigation_run(active_space, 2, false, .95f, .1f));
    assert(focus_calls == 1 && window_focus_calls == 1 && focused_id == 1);
    assert(opacity_calls == 4);
    assert(batch_calls == 2);
    assert(effects[0].alpha == .95f && effects[0].duration == 0);
    assert(effects[2].alpha == 1.0f && effects[2].duration == .1f);
    assert(effects[3].alpha == .975f);
    assert(windows[0].opacity == 0 && windows[1].opacity == 0);
    assert(g_window_manager.active_window_opacity == 1.0f);
    assert(g_window_manager.normal_window_opacity == .975f);

    // At key-repeat speed every navigation survives but no new fades start.
    expect_fade_started = false;
    opacity_calls = 0;
    for (int i = 0; i < 30; ++i) {
        timestamp += 30000000;
        assert(space_navigation_run(active_space, i % 2 ? 2 : 1, false, .95f, .1f));
    }

    assert(focus_calls == 31 && opacity_calls == 0);
    assert(batch_calls == 8); // Repeats cancel the running effect, then skip the Dock.

    timestamp += 200000000;
    assert(space_navigation_run(active_space, 1, false, .95f, .1f));
    assert(opacity_calls == 4);

    // Only a display whose effect may still run is asked to cancel it.
    batch_calls = 0;
    timestamp += 30000000;
    assert(space_navigation_run(active_space, 2, false, .95f, 0));
    assert(batch_calls == 1);

    assert(space_navigation_run(active_space, 3, false, .95f, 0));
    assert(batch_calls == 1);

    timestamp += 300000000;
    assert(space_navigation_run(active_space, 2, false, .95f, 0));
    assert(batch_calls == 1);

    reset();

    windows[0].opacity = .6f;
    assert(space_navigation_run(active_space, 2, false, .95f, .1f));
    assert(opacity_calls == 2 && effects[0].id == 2 && effects[1].id == 2);
    assert(windows[0].opacity == .6f); // Never brighten custom transparent windows.

    reset();

    focus_success = false;
    assert(!space_navigation_run(active_space, 2, false, .95f, .1f));
    assert(window_focus_calls == 0 && space_navigation_last_time == 0);
    assert(opacity_calls == 4 && effects[2].duration == 0 && effects[3].duration == 0);
    assert(effects[2].alpha == .975f && effects[3].alpha == .975f);

    reset();

    opacity_fail_at = 1;
    assert(!space_navigation_run(active_space, 2, false, .95f, .1f));
    assert(focus_calls == 0 && opacity_calls == 4 && effects[2].duration == 0);

    reset();

    opacity_fail_at = 3;
    assert(!space_navigation_run(active_space, 2, false, .95f, .1f));
    assert(focus_calls == 1 && opacity_calls == 6); // Retry restoration and report failure.
    assert(effects[4].duration == 0 && effects[5].duration == 0);

    reset();

    visible = true;
    assert(space_navigation_run(active_space, 3, false, .95f, .1f));
    assert(opacity_calls == 0 && display_calls == 1 && window_focus_calls == 1);

    reset();

    assert(space_navigation_run(active_space, 1, true, .95f, .1f));
    assert(move_calls == 0 && focus_calls == 0 && window_focus_calls == 0 && opacity_calls == 0);

    mission_control = true;
    assert(!space_navigation_run(active_space, 2, true, .95f, .1f));
    assert(move_calls == 0 && focus_calls == 0 && opacity_calls == 0);

    reset();

    windows[0].flags = WINDOW_MINIMIZE;
    windows[1].flags = WINDOW_STICKY;
    assert(space_navigation_run(active_space, 2, false, .95f, .1f));
    assert(window_focus_calls == 0 && opacity_calls == 0 && focus_calls == 1);

    reset();

    assert(space_navigation_run(active_space, 2, true, .95f, 0));
    assert(move_calls == 1 && focused_id == 1 && opacity_calls == 0);
    assert(click_raise_calls == 1); // A moved window keeps window --focus's raise.

    reset();

    fullscreen = true;
    assert(!space_navigation_run(active_space, 2, true, .95f, .1f));
    assert(move_calls == 0 && focus_calls == 0);

    reset();

    // Every navigation drops a deferred focus of the previous one, even when
    // its destination has no window to focus; staying put drops nothing.
    single_window = true;
    windows[0].flags = windows[1].flags = WINDOW_MINIMIZE;
    assert(space_navigation_run(active_space, 2, false, .95f, 0));
    assert(focus_cancels == 1 && window_focus_calls == 0);
    assert(space_navigation_run(active_space, active_space, false, .95f, 0));
    assert(focus_cancels == 1);

    reset();

    // The activation handler reuses the window navigation focused.
    assert(space_navigation_run(active_space, 2, false, .95f, 0));
    assert(noted_id == 1 && raise_calls == 0);

    reset();

    // Another window of the application is visible on the other display: raise.
    other_window_space = 3;
    visible_space = 3;
    assert(space_navigation_run(active_space, 2, false, .95f, 0));
    assert(raise_calls == 1 && focused_id == 1);
    assert(click_raise_calls == 0); // No synthesized click for a late application.
    assert(switches_at_query == 0); // Asked before the switch, not after.

    reset();

    other_window_space = visible_space = 3;
    expect_fade_started = true;
    assert(space_navigation_run(active_space, 2, false, .95f, .1f));
    assert(raise_calls == 1 && opacity_calls == 4);

    reset();

    reduce_motion = true;
    assert(space_navigation_run(active_space, 2, false, .95f, .1f));
    assert(opacity_calls == 0 && focus_calls == 1 && batch_calls == 0);

    reset();

    // Relative navigation starts from the last switch while WindowServer's
    // active display briefly points elsewhere.
    assert(space_navigation_current_space(3) == 3);
    assert(space_navigation_run(active_space, 2, false, .95f, 0));
    visible_space = 2;
    assert(space_navigation_current_space(3) == 2);
    assert(space_navigation_current_space(2) == 2);

    timestamp += 500000000;
    seconds_since_click = 0.2;
    assert(space_navigation_current_space(3) == 3); // A click after the switch.

    seconds_since_click = 1000.0;
    assert(space_navigation_current_space(3) == 2);

    timestamp += 500000000;
    assert(space_navigation_current_space(3) == 3); // The anchor expired.

    timestamp -= 500000000;
    visible_space = 0;
    assert(space_navigation_current_space(3) == 3); // Its space was left.

    visible_space = 2;
    space_navigation_forget();
    assert(space_navigation_current_space(3) == 3);

    reset();

    focus_success = false;
    assert(!space_navigation_run(active_space, 2, false, .95f, 0));
    visible_space = 2;
    assert(space_navigation_current_space(3) == 3); // Failed switches set none.

    // Steps wrap in both directions and across several laps.
    assert(space_navigation_step_index(1, 11, 1) == 2);
    assert(space_navigation_step_index(11, 11, 1) == 1);
    assert(space_navigation_step_index(1, 11, -1) == 11);
    assert(space_navigation_step_index(7, 11, 0) == 7);
    assert(space_navigation_step_index(3, 11, -25) == 11);
    assert(space_navigation_step_index(3, 11, 30) == 11);
    assert(space_navigation_step_index(1, 1, -1) == 1);

    test_steps();

    puts("navigation: burst, restoration, custom opacity, display, move, crossfade and step checks passed");

    return 0;
}
