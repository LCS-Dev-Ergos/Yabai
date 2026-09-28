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

#undef TEST_COMMAND
