static void test_raise_queries(void)
{
    reset();

    // Other windows on this display cannot steal focus across displays.
    // Do not ask WindowServer which Space is currently visible for them.
    other_window_space = 2;
    assert(!space_navigation_needs_raise(&windows[0], 1));
    assert(visibility_queries == 0 && current_space_queries == 0);
    assert(display_queries == 1);

    reset();

    // The cross-display browser case still raises, with one display lookup.
    other_window_space = visible_space = 3;
    assert(space_navigation_needs_raise(&windows[0], 1));
    assert(display_queries == 1 && current_space_queries == 1);

    reset();

    // A hidden Space on the other display does not require AXRaise.
    other_window_space = 3;
    assert(!space_navigation_needs_raise(&windows[0], 1));
    assert(display_queries == 1 && current_space_queries == 1);

    reset();

    other_window_space = 0;
    assert(!space_navigation_needs_raise(&windows[0], 1));
    assert(display_queries == 0 && current_space_queries == 0);
}
