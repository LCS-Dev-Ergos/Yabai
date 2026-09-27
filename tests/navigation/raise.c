static void test_raise_queries(void)
{
    reset();

    // Other windows on this display cannot steal focus across displays.
    other_window_space = 2;
    assert(!space_navigation_needs_raise(&windows[0], 1));
    assert(window_list_queries == 1);

    reset();

    // The cross-display browser case still raises, with one window query
    // however many windows the application has.
    other_window_space = visible_space = 3;
    assert(space_navigation_needs_raise(&windows[0], 1));
    assert(window_list_queries == 1);

    reset();

    // A hidden Space on the other display does not require AXRaise.
    other_window_space = 3;
    assert(!space_navigation_needs_raise(&windows[0], 1));
    assert(window_list_queries == 1);

    reset();

    // Neither does a minimized window on the visible one.
    other_window_space = visible_space = 3;
    windows[1].flags = WINDOW_MINIMIZE;
    assert(!space_navigation_needs_raise(&windows[0], 1));

    reset();

    // With a single display there is nothing to ask.
    single_display = true;
    other_window_space = visible_space = 3;
    assert(!space_navigation_needs_raise(&windows[0], 1));
    assert(window_list_queries == 0);
}
