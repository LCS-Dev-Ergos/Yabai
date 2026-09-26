#include "fixture.h"

int main(void)
{
    reset();

    assert(space_navigation_run(2, false, .95f, .1f));
    assert(focus_calls == 1 && raise_calls == 1 && raised_id == 1);
    assert(opacity_calls == 4);
    assert(effects[0].alpha == .95f && effects[0].duration == 0);
    assert(effects[2].alpha == 1.0f && effects[2].duration == .1f);
    assert(effects[3].alpha == .975f);
    assert(windows[0].opacity == 0 && windows[1].opacity == 0);
    assert(g_window_manager.active_window_opacity == 1.0f);
    assert(g_window_manager.normal_window_opacity == .975f);

    // At key-repeat speed every navigation survives but no new fades start.
    opacity_calls = 0;
    for (int i = 0; i < 30; ++i) {
        timestamp += 30000000;
        assert(space_navigation_run(i % 2 ? 2 : 1, false, .95f, .1f));
    }

    assert(focus_calls == 31 && opacity_calls == 0);

    timestamp += 200000000;
    assert(space_navigation_run(1, false, .95f, .1f));
    assert(opacity_calls == 4);

    reset();

    windows[0].opacity = .6f;
    assert(space_navigation_run(2, false, .95f, .1f));
    assert(opacity_calls == 2 && effects[0].id == 2 && effects[1].id == 2);
    assert(windows[0].opacity == .6f); // Never brighten custom transparent windows.

    reset();

    focus_success = false;
    assert(!space_navigation_run(2, false, .95f, .1f));
    assert(raise_calls == 0 && space_navigation_last_time == 0);
    assert(opacity_calls == 4 && effects[2].duration == 0 && effects[3].duration == 0);
    assert(effects[2].alpha == .975f && effects[3].alpha == .975f);

    reset();

    opacity_fail_at = 1;
    assert(!space_navigation_run(2, false, .95f, .1f));
    assert(focus_calls == 0 && opacity_calls == 4 && effects[2].duration == 0);

    reset();

    opacity_fail_at = 3;
    assert(!space_navigation_run(2, false, .95f, .1f));
    assert(focus_calls == 1 && opacity_calls == 4); // Report incomplete restoration.

    reset();

    visible = true;
    assert(space_navigation_run(3, false, .95f, .1f));
    assert(opacity_calls == 0 && display_calls == 1 && raise_calls == 1);

    reset();

    assert(space_navigation_run(1, true, .95f, .1f));
    assert(move_calls == 0 && focus_calls == 0 && raise_calls == 0 && opacity_calls == 0);

    mission_control = true;
    assert(!space_navigation_run(2, true, .95f, .1f));
    assert(move_calls == 0 && focus_calls == 0 && opacity_calls == 0);

    reset();

    windows[0].flags = WINDOW_MINIMIZE;
    windows[1].flags = WINDOW_STICKY;
    assert(space_navigation_run(2, false, .95f, .1f));
    assert(raise_calls == 0 && opacity_calls == 0 && focus_calls == 1);

    reset();

    assert(space_navigation_run(2, true, .95f, 0));
    assert(move_calls == 1 && raised_id == 1 && opacity_calls == 0);

    reset();

    fullscreen = true;
    assert(!space_navigation_run(2, true, .95f, .1f));
    assert(move_calls == 0 && focus_calls == 0);

    puts("navigation: burst, restoration, custom opacity, display and move checks passed");

    return 0;
}
