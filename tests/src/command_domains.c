// Domain dispatch and representative state changes from NUL-separated requests.
// Display, Space and focused-window selection are fixed by the test harness.

static char *test_command_run(const char *const *tokens, int count)
{
    size_t size = 1;
    for (int i = 0; i < count; ++i) size += strlen(tokens[i]) + 1;

    char *message = calloc(size, 1);
    char *cursor = message;
    for (int i = 0; i < count; ++i) {
        size_t length = strlen(tokens[i]);
        memcpy(cursor, tokens[i], length);
        cursor += length + 1;
    }

    char *output = NULL;
    size_t output_size = 0;
    FILE *response = open_memstream(&output, &output_size);
    handle_message(response, message);
    fclose(response);
    free(message);
    return output;
}

#define TEST_COMMAND(...) test_command_run((const char *[]) { __VA_ARGS__ }, \
                                           (int)(sizeof((const char *[]) { __VA_ARGS__ }) / sizeof(char *)))

TEST_FUNC(command_domain_errors,
{
    struct command_case {
        const char *domain;
        const char *expected;
    } cases[] = {
        { "display", "\x07unknown command '--invalid' for domain 'display'\n" },
        { "space",   "\x07unknown command '--invalid' for domain 'space'\n" },
        { "window",  "\x07unknown command '--invalid' for domain 'window'\n" },
        { "query",   "\x07unknown command '--invalid' for domain 'query'\n" },
        { "rule",    "\x07unknown command '--invalid' for domain 'rule'\n" },
        { "signal",  "\x07unknown command '--invalid' for domain 'signal'\n" },
        { "config",  "\x07unknown command '--invalid' for domain 'config'\n" },
    };

    for (int i = 0; i < array_count(cases); ++i) {
        const char *tokens[] = { cases[i].domain, "--invalid" };
        char *output = test_command_run(tokens, array_count(tokens));
        if (strcmp(output, cases[i].expected) != 0) {
            printf("                   %s returned '%s'\n", cases[i].domain, output);
            result = false;
        }
        free(output);
    }
});

TEST_FUNC(command_domain_state_changes,
{
    bool saved_verbose = g_verbose;
    struct display_label *saved_display_labels = g_display_manager.labels;
    struct space_label *saved_space_labels = g_space_manager.labels;
    struct rule *saved_rules = g_window_manager.rules;
    struct signal *saved_signals = g_signal_event[SIGNAL_APPLICATION_LAUNCHED];
    int rule_count = buf_len(g_window_manager.rules);
    int signal_count = buf_len(g_signal_event[SIGNAL_APPLICATION_LAUNCHED]);
    char *output;

    output = TEST_COMMAND("config", "debug_output", "on");
    TEST_CHECK(strcmp(output, "") == 0, true);
    TEST_CHECK(g_verbose, true);
    free(output);
    output = TEST_COMMAND("config", "debug_output");
    TEST_CHECK(strcmp(output, "on\n") == 0, true);
    free(output);

    output = TEST_COMMAND("display", "--label", "codex-display");
    TEST_CHECK(strcmp(output, "") == 0, true);
    struct display_label *display_label = display_manager_get_label_for_display(&g_display_manager, 7);
    TEST_CHECK(display_label != NULL, true);
    if (display_label) TEST_CHECK(strcmp(display_label->label, "codex-display") == 0, true);
    free(output);

    output = TEST_COMMAND("space", "--label", "codex-space");
    TEST_CHECK(strcmp(output, "") == 0, true);
    struct space_label *space_label = space_manager_get_label_for_space(&g_space_manager, 42);
    TEST_CHECK(space_label != NULL, true);
    if (space_label) TEST_CHECK(strcmp(space_label->label, "codex-space") == 0, true);
    free(output);

    window_clear_flag(&test_command_window, WINDOW_FLOAT);
    output = TEST_COMMAND("window", "--toggle", "float");
    TEST_CHECK(strcmp(output, "") == 0, true);
    TEST_CHECK(window_check_flag(&test_command_window, WINDOW_FLOAT), true);
    free(output);

    output = TEST_COMMAND("rule", "--add", "label=codex-rule", "app=Editor", "manage=on");
    TEST_CHECK(strcmp(output, "") == 0, true);
    TEST_CHECK(buf_len(g_window_manager.rules), rule_count + 1);
    free(output);

    output = TEST_COMMAND("signal", "--add", "label=codex-signal", "event=application_launched", "action=true");
    TEST_CHECK(strcmp(output, "") == 0, true);
    TEST_CHECK(buf_len(g_signal_event[SIGNAL_APPLICATION_LAUNCHED]), signal_count + 1);
    free(output);

    struct table saved_views = g_space_manager.view;
    struct view query_view = { .sid = 42 };
    table_init(&g_space_manager.view, 7, hash_view, compare_view);
    table_add(&g_space_manager.view, &query_view.sid, &query_view);
    output = TEST_COMMAND("query", "--spaces", "id", "--space");
    TEST_CHECK(strcmp(output, "{\n\t\"id\":42\n}\n") == 0, true);
    free(output);
    table_free(&g_space_manager.view);
    g_space_manager.view = saved_views;

    output = TEST_COMMAND("rule", "--remove", "codex-rule");
    TEST_CHECK(strcmp(output, "") == 0, true);
    TEST_CHECK(buf_len(g_window_manager.rules), rule_count);
    free(output);

    output = TEST_COMMAND("signal", "--remove", "codex-signal");
    TEST_CHECK(strcmp(output, "") == 0, true);
    TEST_CHECK(buf_len(g_signal_event[SIGNAL_APPLICATION_LAUNCHED]), signal_count);
    free(output);

    space_manager_remove_label_for_space(&g_space_manager, 42);
    display_manager_remove_label_for_display(&g_display_manager, 7);
    if (!saved_space_labels) {
        buf_free(g_space_manager.labels);
        g_space_manager.labels = NULL;
    }
    if (!saved_display_labels) {
        buf_free(g_display_manager.labels);
        g_display_manager.labels = NULL;
    }
    if (!saved_rules) {
        buf_free(g_window_manager.rules);
        g_window_manager.rules = NULL;
    }
    if (!saved_signals) {
        buf_free(g_signal_event[SIGNAL_APPLICATION_LAUNCHED]);
        g_signal_event[SIGNAL_APPLICATION_LAUNCHED] = NULL;
    }
    window_clear_flag(&test_command_window, WINDOW_FLOAT);
    g_verbose = saved_verbose;
});

// The fade curve of the navigation overlays: the default, each listed name
// through get and set, and nothing else.
TEST_FUNC(config_navigation_fade_curve,
{
    int saved = g_window_manager.navigation_fade_curve;
    char *output;

    g_window_manager.navigation_fade_curve = SPACE_SNAPSHOT_CURVE_SMOOTH;
    output = TEST_COMMAND("config", "navigation_fade_curve");
    TEST_CHECK(strcmp(output, "smooth\n") == 0, true);
    free(output);

    output = TEST_COMMAND("config", "navigation_fade_curve", "ease_out");
    TEST_CHECK(strcmp(output, "") == 0, true);
    TEST_CHECK(g_window_manager.navigation_fade_curve, SPACE_SNAPSHOT_CURVE_EASE_OUT);
    free(output);
    output = TEST_COMMAND("config", "navigation_fade_curve");
    TEST_CHECK(strcmp(output, "ease_out\n") == 0, true);
    free(output);

    output = TEST_COMMAND("config", "navigation_fade_curve", "smooth");
    TEST_CHECK(strcmp(output, "") == 0, true);
    TEST_CHECK(g_window_manager.navigation_fade_curve, SPACE_SNAPSHOT_CURVE_SMOOTH);
    free(output);

    // A refused value leaves the setting as it was; names match exactly.
    g_window_manager.navigation_fade_curve = SPACE_SNAPSHOT_CURVE_EASE_OUT;
    const char *invalid[] = { "cubic", "ease-out", "ease_out2", "Smooth", "0", "ease" };
    for (int i = 0; i < array_count(invalid); ++i) {
        char expected[160];
        snprintf(expected, sizeof(expected),
                 "\x07unknown value '%s' given to command 'navigation_fade_curve' for domain 'config'\n", invalid[i]);
        output = TEST_COMMAND("config", "navigation_fade_curve", invalid[i]);
        if (strcmp(output, expected) != 0) {
            printf("                   %s returned '%s'\n", invalid[i], output);
            result = false;
        }
        TEST_CHECK(g_window_manager.navigation_fade_curve, SPACE_SNAPSHOT_CURVE_EASE_OUT);
        free(output);
    }

    g_window_manager.navigation_fade_curve = saved;
});

// The veil's blur radius: off by default, any integer from 0 to 100 through
// get and set, and nothing else.
TEST_FUNC(config_navigation_veil_blur,
{
    int saved = g_window_manager.navigation_veil_blur;
    char *output;

    // Get prints the radius; 0, the default, means off.
    g_window_manager.navigation_veil_blur = 0;
    output = TEST_COMMAND("config", "navigation_veil_blur");
    TEST_CHECK(strcmp(output, "0\n") == 0, true);
    free(output);

    const int accepted[] = { 1, 40, 99, 100, 0 };
    for (int i = 0; i < array_count(accepted); ++i) {
        char value[8], expected[8];
        snprintf(value, sizeof(value), "%d", accepted[i]);
        snprintf(expected, sizeof(expected), "%d\n", accepted[i]);
        output = TEST_COMMAND("config", "navigation_veil_blur", value);
        TEST_CHECK(strcmp(output, "") == 0, true);
        TEST_CHECK(g_window_manager.navigation_veil_blur, accepted[i]);
        free(output);
        output = TEST_COMMAND("config", "navigation_veil_blur");
        TEST_CHECK(strcmp(output, expected) == 0, true);
        free(output);
    }

    // A refused value, out of range or not an integer, leaves the setting as
    // it was.
    g_window_manager.navigation_veil_blur = 40;
    const char *invalid[] = { "101", "1000", "99999999999", "-1", "1.5", "0x10", "off", "on", "blur", "1e1", "+5" };
    for (int i = 0; i < array_count(invalid); ++i) {
        char expected[160];
        snprintf(expected, sizeof(expected),
                 "\x07unknown value '%s' given to command 'navigation_veil_blur' for domain 'config'\n", invalid[i]);
        output = TEST_COMMAND("config", "navigation_veil_blur", invalid[i]);
        if (strcmp(output, expected) != 0) {
            printf("                   %s returned '%s'\n", invalid[i], output);
            result = false;
        }
        TEST_CHECK(g_window_manager.navigation_veil_blur, 40);
        free(output);
    }

    g_window_manager.navigation_veil_blur = saved;
});

// A value is refused with the usual message and leaves the setting as it was.
static bool config_refuses(const char *setting, const char *value)
{
    char expected[192];
    snprintf(expected, sizeof(expected),
             "\x07unknown value '%s' given to command '%s' for domain 'config'\n", value, setting);
    char *output = TEST_COMMAND("config", setting, value);
    bool refused = strcmp(output, expected) == 0;
    if (!refused) printf("                   %s %s returned '%s'\n", setting, value, output);
    free(output);
    return refused;
}

// Whether a get prints `expected`.
static bool config_prints(const char *setting, const char *expected)
{
    char *output = TEST_COMMAND("config", setting);
    bool match = strcmp(output, expected) == 0;
    if (!match) printf("                   %s printed '%s'\n", setting, output);
    free(output);
    return match;
}

// Whether a set is accepted silently.
static bool config_sets(const char *setting, const char *value)
{
    char *output = TEST_COMMAND("config", setting, value);
    bool accepted = strcmp(output, "") == 0;
    if (!accepted) printf("                   %s %s returned '%s'\n", setting, value, output);
    free(output);
    return accepted;
}

// The navigation effect settings that requests without their own effect use,
// and the switch and pressure fallback that apply to every step: each value
// through get and set, and nothing else.
TEST_FUNC(config_navigation_effect_settings,
{
    bool saved_effect = g_window_manager.navigation_effect;
    int saved_type = g_window_manager.navigation_effect_type;
    float saved_duration = g_window_manager.navigation_effect_duration;
    int saved_fallback = g_window_manager.navigation_pressure_fallback;

    g_window_manager.navigation_effect = true;
    TEST_CHECK(config_prints("navigation_effect", "on\n"), true);
    TEST_CHECK(config_sets("navigation_effect", "off"), true);
    TEST_CHECK(g_window_manager.navigation_effect, false);
    TEST_CHECK(config_prints("navigation_effect", "off\n"), true);
    TEST_CHECK(config_sets("navigation_effect", "on"), true);
    TEST_CHECK(g_window_manager.navigation_effect, true);
    const char *invalid_switch[] = { "toggle", "On", "1", "true", "crossfade" };
    for (int i = 0; i < array_count(invalid_switch); ++i) {
        TEST_CHECK(config_refuses("navigation_effect", invalid_switch[i]), true);
        TEST_CHECK(g_window_manager.navigation_effect, true);
    }

    g_window_manager.navigation_effect_type = SPACE_NAVIGATION_EFFECT_CROSSFADE;
    TEST_CHECK(config_prints("navigation_effect_type", "crossfade\n"), true);
    TEST_CHECK(config_sets("navigation_effect_type", "veil"), true);
    TEST_CHECK(g_window_manager.navigation_effect_type, SPACE_NAVIGATION_EFFECT_VEIL);
    TEST_CHECK(config_prints("navigation_effect_type", "veil\n"), true);
    TEST_CHECK(config_sets("navigation_effect_type", "crossfade"), true);
    TEST_CHECK(g_window_manager.navigation_effect_type, SPACE_NAVIGATION_EFFECT_CROSSFADE);
    const char *invalid_type[] = { "fade", "Veil", "veils", "0.95", "off", "none" };
    for (int i = 0; i < array_count(invalid_type); ++i) {
        TEST_CHECK(config_refuses("navigation_effect_type", invalid_type[i]), true);
        TEST_CHECK(g_window_manager.navigation_effect_type, SPACE_NAVIGATION_EFFECT_CROSSFADE);
    }

    // The duration takes the bounds a request's own duration has: [0, 1].
    g_window_manager.navigation_effect_duration = 0.25f;
    TEST_CHECK(config_prints("navigation_effect_duration", "0.250000\n"), true);
    const char *accepted[] = { "0", "1", "0.4", "0.0", "1.0" };
    const float values[] = { 0.0f, 1.0f, 0.4f, 0.0f, 1.0f };
    for (int i = 0; i < array_count(accepted); ++i) {
        TEST_CHECK(config_sets("navigation_effect_duration", accepted[i]), true);
        TEST_CHECK(g_window_manager.navigation_effect_duration == values[i], true);
    }
    g_window_manager.navigation_effect_duration = 0.25f;
    const char *invalid_duration[] = { "-0.1", "1.01", "2", "nan", "inf", "-inf", "1e999", "0.2s", "off" };
    for (int i = 0; i < array_count(invalid_duration); ++i) {
        TEST_CHECK(config_refuses("navigation_effect_duration", invalid_duration[i]), true);
        TEST_CHECK(g_window_manager.navigation_effect_duration == 0.25f, true);
    }

    g_window_manager.navigation_pressure_fallback = SPACE_NAVIGATION_PRESSURE_VEIL;
    TEST_CHECK(config_prints("navigation_pressure_fallback", "veil\n"), true);
    const char *fallbacks[] = { "keep", "none", "veil" };
    const int fallback_values[] = { SPACE_NAVIGATION_PRESSURE_KEEP, SPACE_NAVIGATION_PRESSURE_NONE, SPACE_NAVIGATION_PRESSURE_VEIL };
    for (int i = 0; i < array_count(fallbacks); ++i) {
        char expected[16];
        snprintf(expected, sizeof(expected), "%s\n", fallbacks[i]);
        TEST_CHECK(config_sets("navigation_pressure_fallback", fallbacks[i]), true);
        TEST_CHECK(g_window_manager.navigation_pressure_fallback, fallback_values[i]);
        TEST_CHECK(config_prints("navigation_pressure_fallback", expected), true);
    }
    const char *invalid_fallback[] = { "crossfade", "off", "skip", "Veil", "1" };
    for (int i = 0; i < array_count(invalid_fallback); ++i) {
        TEST_CHECK(config_refuses("navigation_pressure_fallback", invalid_fallback[i]), true);
        TEST_CHECK(g_window_manager.navigation_pressure_fallback, SPACE_NAVIGATION_PRESSURE_VEIL);
    }

    g_window_manager.navigation_effect = saved_effect;
    g_window_manager.navigation_effect_type = saved_type;
    g_window_manager.navigation_effect_duration = saved_duration;
    g_window_manager.navigation_pressure_fallback = saved_fallback;
});

#undef TEST_COMMAND
