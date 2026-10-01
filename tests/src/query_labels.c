// Labels and scratchpad names are any text a client gives; the JSON that lists
// them escapes them like window titles, so a quote or backslash cannot end the
// string early or break the document.

#define TEST_LABEL_RUN(...) test_command_run((const char *[]) { __VA_ARGS__ }, \
                                             (int)(sizeof((const char *[]) { __VA_ARGS__ }) / sizeof(char *)))

static bool test_label_contains(const char *output, const char *expected)
{
    bool found = output && strstr(output, expected) != NULL;
    if (!found) printf("                   expected %s in '%s'\n", expected, output ? output : "");
    return found;
}

TEST_FUNC(query_label_escape,
{
    if (!g_temp_storage.memory) TEST_CHECK(ts_init(KILOBYTES(64)), true);
    struct display_label *saved_display_labels = g_display_manager.labels;
    struct space_label *saved_space_labels = g_space_manager.labels;
    struct rule *saved_rules = g_window_manager.rules;
    struct signal *saved_signals = g_signal_event[SIGNAL_APPLICATION_LAUNCHED];
    char *output;
    bool found;

    free(TEST_LABEL_RUN("signal", "--add", "label=sig\"nal\\1", "event=application_launched", "action=true"));
    output = TEST_LABEL_RUN("signal", "--list");
    found = test_label_contains(output, "\"label\":\"sig\\\"nal\\\\1\"");
    TEST_CHECK(found, true);
    free(output);
    free(TEST_LABEL_RUN("signal", "--remove", "sig\"nal\\1"));

    free(TEST_LABEL_RUN("rule", "--add", "label=ru\"le", "app=Editor", "scratchpad=pad\\1"));
    output = TEST_LABEL_RUN("rule", "--list");
    found = test_label_contains(output, "\"label\":\"ru\\\"le\"");
    TEST_CHECK(found, true);
    found = test_label_contains(output, "\"scratchpad\":\"pad\\\\1\"");
    TEST_CHECK(found, true);
    free(output);
    free(TEST_LABEL_RUN("rule", "--remove", "ru\"le"));

    free(TEST_LABEL_RUN("display", "--label", "dis\"play"));
    output = TEST_LABEL_RUN("query", "--displays", "label", "--display", "dis\"play");
    found = test_label_contains(output, "\"label\":\"dis\\\"play\"");
    TEST_CHECK(found, true);
    free(output);
    display_manager_remove_label_for_display(&g_display_manager, 7);

    struct table saved_views = g_space_manager.view;
    struct view query_view = { .sid = 42 };
    table_init(&g_space_manager.view, 7, hash_view, compare_view);
    table_add(&g_space_manager.view, &query_view.sid, &query_view);

    free(TEST_LABEL_RUN("space", "--label", "spa\"ce"));
    output = TEST_LABEL_RUN("query", "--spaces", "label", "--space");
    found = test_label_contains(output, "\"label\":\"spa\\\"ce\"");
    TEST_CHECK(found, true);
    free(output);

    space_manager_remove_label_for_space(&g_space_manager, 42);
    table_free(&g_space_manager.view);
    g_space_manager.view = saved_views;

    // A window's scratchpad name, as window queries print it.
    output = NULL;
    size_t output_size = 0;
    FILE *response = open_memstream(&output, &output_size);
    struct window window = { .id = 601, .scratchpad = "scr\"atch" };
    window_serialize(response, &window, WINDOW_PROPERTY_SCRATCHPAD);
    fclose(response);

    found = test_label_contains(output, "\"scratchpad\":\"scr\\\"atch\"");
    TEST_CHECK(found, true);
    free(output);

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
    ts_reset();
});

#undef TEST_LABEL_RUN
