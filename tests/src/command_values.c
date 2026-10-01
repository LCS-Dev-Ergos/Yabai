// The numbers commands accept: decimals or whole numbers, finite, within the
// setting's range. A refused value leaves the setting as it was and touches no
// window.

static bool test_command_expect(const char *expected, const char *const *tokens, int count)
{
    char *output = test_command_run(tokens, count);
    bool matches = strcmp(output, expected) == 0;
    if (!matches) {
        printf("                   %s %s %s returned '%s'\n", tokens[0], tokens[1], count > 2 ? tokens[2] : "", output);
    }
    free(output);
    return matches;
}

// TEST_CHECK evaluates its arguments again to report a failure, so the command
// runs once, before its check.
#define TEST_EXPECT_OUTPUT(expected, ...)                                                      \
    do {                                                                                       \
        bool matches = test_command_expect(expected, (const char *[]) { __VA_ARGS__ },         \
                                           (int)(sizeof((const char *[]) { __VA_ARGS__ }) /    \
                                                 sizeof(char *)));                             \
        TEST_CHECK(matches, true);                                                             \
    } while (0)

static const char *test_unknown_value(const char *value, const char *command, const char *domain)
{
    static char expected[256];
    snprintf(expected, sizeof(expected), "\x07unknown value '%s' given to command '%s' for domain '%s'\n",
             value, command, domain);
    return expected;
}

TEST_FUNC(config_decimal_values,
{
    float saved_opacity_duration = g_window_manager.window_opacity_duration;
    float saved_animation_duration = g_window_manager.window_animation_duration;
    float saved_normal_opacity = g_window_manager.normal_window_opacity;
    float saved_active_opacity = g_window_manager.active_window_opacity;
    float saved_menubar_opacity = g_window_manager.menubar_opacity;

    // Whole numbers are decimals too.
    struct { const char *value; float expected; } accepted[] = {
        { "0", 0.0f }, { "1", 1.0f }, { "2", 2.0f }, { "0.25", 0.25f }
    };
    for (int i = 0; i < array_count(accepted); ++i) {
        g_window_manager.window_opacity_duration = 0.5f;
        TEST_EXPECT_OUTPUT("", "config", "window_opacity_duration", accepted[i].value);
        TEST_CHECK(g_window_manager.window_opacity_duration == accepted[i].expected, true);
    }

    // A negative or non-finite duration is refused: an animation would never
    // end, and Dock would drop every focus fade.
    const char *durations[] = { "-1", "-0.5", "nan", "-nan", "inf", "-inf", "1e40" };
    for (int i = 0; i < array_count(durations); ++i) {
        g_window_manager.window_opacity_duration = 0.25f;
        TEST_EXPECT_OUTPUT(test_unknown_value(durations[i], "window_opacity_duration", "config"),
                           "config", "window_opacity_duration", durations[i]);
        TEST_CHECK(g_window_manager.window_opacity_duration == 0.25f, true);

        g_window_manager.window_animation_duration = 0.35f;
        TEST_EXPECT_OUTPUT(test_unknown_value(durations[i], "window_animation_duration", "config"),
                           "config", "window_animation_duration", durations[i]);
        TEST_CHECK(g_window_manager.window_animation_duration == 0.35f, true);
    }

    // 0 turns animations off and needs neither the scripting addition nor
    // Screen Recording.
    g_window_manager.window_animation_duration = 0.35f;
    TEST_EXPECT_OUTPUT("", "config", "window_animation_duration", "0");
    TEST_CHECK(g_window_manager.window_animation_duration == 0.0f, true);

    // Opacities take whole numbers within their range.
    TEST_EXPECT_OUTPUT("", "config", "normal_window_opacity", "1");
    TEST_CHECK(g_window_manager.normal_window_opacity == 1.0f, true);
    TEST_EXPECT_OUTPUT("", "config", "normal_window_opacity", "0.5");
    TEST_CHECK(g_window_manager.normal_window_opacity == 0.5f, true);

    struct { const char *command; const char *value; } refused[] = {
        { "normal_window_opacity", "0" }, { "normal_window_opacity", "2" }, { "normal_window_opacity", "nan" },
        { "active_window_opacity", "0" }, { "active_window_opacity", "1.5" }, { "active_window_opacity", "-nan" },
        { "menubar_opacity", "-0.1" }, { "menubar_opacity", "2" }, { "menubar_opacity", "nan" },
        { "menubar_opacity", "inf" }
    };
    g_window_manager.active_window_opacity = 0.75f;
    g_window_manager.menubar_opacity = 0.75f;
    for (int i = 0; i < array_count(refused); ++i) {
        TEST_EXPECT_OUTPUT(test_unknown_value(refused[i].value, refused[i].command, "config"),
                           "config", refused[i].command, refused[i].value);
    }
    TEST_CHECK(g_window_manager.normal_window_opacity == 0.5f, true);
    TEST_CHECK(g_window_manager.active_window_opacity == 0.75f, true);
    TEST_CHECK(g_window_manager.menubar_opacity == 0.75f, true);

    g_window_manager.window_opacity_duration = saved_opacity_duration;
    g_window_manager.window_animation_duration = saved_animation_duration;
    g_window_manager.normal_window_opacity = saved_normal_opacity;
    g_window_manager.active_window_opacity = saved_active_opacity;
    g_window_manager.menubar_opacity = saved_menubar_opacity;
});

TEST_FUNC(window_value_bounds,
{
    // A ratio, position or size that is not finite, and a grid without rows or
    // columns or with a negative cell, are refused before the window is touched:
    // a NaN split ratio would stay in the tree.
    struct { const char *command; const char *value; } refused[] = {
        { "--ratio", "abs:nan" }, { "--ratio", "rel:-nan" }, { "--ratio", "abs:inf" },
        { "--move", "abs:inf:0" }, { "--move", "rel:0:nan" },
        { "--resize", "abs:nan:10" }, { "--resize", "right:10:-inf" },
        { "--grid", "0:2:0:0:1:1" }, { "--grid", "2:0:0:0:1:1" }, { "--grid", "-1:2:0:0:1:1" },
        { "--grid", "2:2:-1:0:1:1" }, { "--grid", "2:2:0:0:-1:1" },
        { "--opacity", "nan" }, { "--opacity", "2" }
    };
    for (int i = 0; i < array_count(refused); ++i) {
        TEST_EXPECT_OUTPUT(test_unknown_value(refused[i].value, refused[i].command, "window"),
                           "window", refused[i].command, refused[i].value);
    }

    // A whole number is an opacity: this one reaches the scripting addition,
    // which the tests do not run.
    TEST_EXPECT_OUTPUT("\x07" "could not change opacity of window with id '501' due to an error with the scripting-addition.\n",
                       "window", "--opacity", "1");
});

TEST_FUNC(rule_grid_bounds,
{
    int count = buf_len(g_window_manager.rules);
    const char *grids[] = { "0:2:0:0:1:1", "2:0:0:0:1:1", "2:2:-1:0:1:1", "2:2:0:0:1:-1" };

    for (int i = 0; i < array_count(grids); ++i) {
        char pair[64], expected[128];
        snprintf(pair, sizeof(pair), "grid=%s", grids[i]);
        snprintf(expected, sizeof(expected), "\x07invalid value '%s' for key 'grid'\n", grids[i]);
        TEST_EXPECT_OUTPUT(expected, "rule", "--add", "app=Editor", pair);
        TEST_CHECK(buf_len(g_window_manager.rules), count);
    }
});

#undef TEST_EXPECT_OUTPUT
