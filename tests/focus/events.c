#include "fixture.h"

int main(void)
{
    reset();

    // A queued AX notification already identifies the actual focused window.
    window_manager_handle_front_focus(&app);
    assert(ax_calls == 0);
    assert(focus_calls == 1 && signals == 1);
    assert(g_window_manager.focused_window_id == 2);

    reset();

    frontmost = false;
    window_manager_handle_front_focus(&app);
    assert(ax_calls == 0 && focus_calls == 0 && signals == 0);
    assert(g_window_manager.focused_window_id == 1);

    // Without a usable observation, preserve the ordinary AX fallback.
    for (int scenario = 0; scenario < 5; ++scenario) {
        reset();

        if (scenario == 0) __pending_window_focus_id = 0;
        if (scenario == 1) windows[1].application = &other_app;
        if (scenario == 2) windows[1].id_ptr = NULL;
        if (scenario == 3) windows[1].flags = WINDOW_MINIMIZE;
        if (scenario == 4) visible = false;

        window_manager_handle_front_focus(&app);
        assert(ax_calls == 1 && focus_calls == 1 && signals == 1);
    }

    reset();

    __pending_window_focus_id = 0;
    ax_id = 0;
    window_manager_handle_front_focus(&app);
    assert(ax_calls == 1 && focus_calls == 0 && signals == 0);
    assert(opacity_calls == 1 && g_window_manager.focused_window_id == 0);
    assert(g_window_manager.last_window_id == 1);
    assert(g_window_manager.focused_window_psn == app.psn);
    assert(g_mouse_state.ffm_window_id == 0);

    reset();

    __pending_window_focus_id = 0;
    ax_id = 99;
    window_manager_handle_front_focus(&app);
    assert(ax_calls == 1 && lost_id == 99);
    assert(opacity_calls == 1 && focus_calls == 0 && signals == 0);

    reset();

    window_focus_note(1);
    window_focus_note(2);
    window_focus_consume(1);
    assert(__pending_window_focus_id == 2);
    window_focus_consume(2);
    assert(__pending_window_focus_id == 0);

    window_manager_handle_front_focus(&app);
    assert(ax_calls == 1); // Consumed notifications cannot become a focus cache.

    puts("focus: observed notifications, stale activations and AX fallback passed");
    return 0;
}
