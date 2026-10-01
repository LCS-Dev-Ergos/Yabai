// Rule and signal patterns compile on the event loop, and regcomp's automaton
// can grow far faster than the pattern: nested bounds multiply its positions,
// and optional parts that follow one another connect every pair of them. A
// pattern whose automaton would cost more than the longest literal a request
// can carry is refused before regcomp runs. No refused pattern here is ever
// compiled: the worst would hold the test for seconds and gigabytes.

#define TEST_PATTERN_RUN(...) test_command_run((const char *[]) { __VA_ARGS__ }, \
                                               (int)(sizeof((const char *[]) { __VA_ARGS__ }) / sizeof(char *)))

static char *test_pattern_repeat(const char *prefix, const char *unit, int count, const char *suffix)
{
    size_t prefix_length = strlen(prefix);
    size_t unit_length = strlen(unit);
    size_t suffix_length = strlen(suffix);
    char *pattern = malloc(prefix_length + unit_length * count + suffix_length + 1);

    char *cursor = pattern;
    memcpy(cursor, prefix, prefix_length);
    cursor += prefix_length;
    for (int i = 0; i < count; ++i, cursor += unit_length) memcpy(cursor, unit, unit_length);
    memcpy(cursor, suffix, suffix_length + 1);

    return pattern;
}

// Whether `pattern` falls on the expected side of the limit.
static bool test_pattern_within(const char *pattern, bool expected)
{
    double cost = pattern_cost(pattern);
    bool within = cost <= PATTERN_COST_LIMIT;
    if (within != expected) {
        printf("                   '%.40s%s' costs %.0f\n", pattern, strlen(pattern) > 40 ? "..." : "", cost);
    }
    return within == expected;
}

static bool test_pattern_contains(const char *output, const char *expected)
{
    bool found = output && strstr(output, expected) != NULL;
    if (!found) printf("                   expected '%s' in '%s'\n", expected, output ? output : "");
    return found;
}

TEST_FUNC(pattern_cost_bounds,
{
    // What configurations write, and well beyond it.
    const char *accepted[] = {
        "", "^Finder$", "()", "a{,3}", "\\(a\\{255\\}\\)", "[]a-z{}()|*]+", "[[:alpha:]][^[:space:]]*",
        "^(Calculator|Software Update|Dictionary|System Settings|Photo Booth|Archive Utility)$",
        ".*foo.*bar.*baz.*", "^.{0,40}(Preferences|Settings).{0,40}$", "(a?){255}", "(a{50}){50}", "a{0,255}",
    };
    for (int i = 0; i < array_count(accepted); ++i) {
        bool held = test_pattern_within(accepted[i], true);
        TEST_CHECK(held, true);
    }

    // Any literal a request can carry, and an alternation of 300 names.
    char *literal = test_pattern_repeat("", "a", 64 * 1024, "");
    bool held = test_pattern_within(literal, true);
    TEST_CHECK(held, true);
    free(literal);

    char *names = test_pattern_repeat("^(", "Application|", 300, "Last)$");
    held = test_pattern_within(names, true);
    TEST_CHECK(held, true);
    free(names);

    // Nested bounds, optional runs and wide alternations.
    const char *refused[] = {
        "((a{255}){255}){255}", "((a{50}){50}){50}", "(a{0,255}){0,64}", "((a?){100}){10}", "(((a|b){255}){255})",
        "x((a{255}){255}){255}y|z",
    };
    for (int i = 0; i < array_count(refused); ++i) {
        held = test_pattern_within(refused[i], false);
        TEST_CHECK(held, true);
    }

    char *optional = test_pattern_repeat("", "a?", 600, "");
    held = test_pattern_within(optional, false);
    TEST_CHECK(held, true);
    free(optional);

    char *alternation = test_pattern_repeat("(", "a|", 1000, "b)");
    held = test_pattern_within(alternation, false);
    TEST_CHECK(held, true);
    free(alternation);

    // Nesting past the parser's depth, and a request full of bounds, which
    // must not take long to refuse.
    char *nested = test_pattern_repeat("", "(", PATTERN_MAX_DEPTH + 1, "a");
    held = test_pattern_within(nested, false);
    TEST_CHECK(held, true);
    free(nested);

    char *bounds = test_pattern_repeat("", "a{255}", 10000, "");
    uint64_t start = read_os_timer();
    held = test_pattern_within(bounds, false);
    double elapsed_ms = (double) (read_os_timer() - start) / 1e6;
    TEST_CHECK(held, true);
    TEST_CHECK(elapsed_ms < 100.0, true);
    free(bounds);
});

TEST_FUNC(pattern_commands,
{
    struct rule *saved_rules = g_window_manager.rules;
    struct signal *saved_signals = g_signal_event[SIGNAL_WINDOW_CREATED];
    int rule_count = buf_len(g_window_manager.rules);
    int signal_count = buf_len(g_signal_event[SIGNAL_WINDOW_CREATED]);

    // Refused before regcomp runs, and nothing is added.
    char *output = TEST_PATTERN_RUN("rule", "--add", "label=complex", "app=((a{50}){50}){50}");
    bool found = test_pattern_contains(output, "\x07regex pattern for key 'app' is too complex\n");
    TEST_CHECK(found, true);
    TEST_CHECK(buf_len(g_window_manager.rules), rule_count);
    free(output);

    output = TEST_PATTERN_RUN("signal", "--add", "label=complex", "event=window_created", "action=true",
                              "title=((a{50}){50}){50}");
    found = test_pattern_contains(output, "\x07regex pattern for key 'title' is too complex\n");
    TEST_CHECK(found, true);
    TEST_CHECK(buf_len(g_signal_event[SIGNAL_WINDOW_CREATED]), signal_count);
    free(output);

    // A syntax error is still reported as one.
    output = TEST_PATTERN_RUN("rule", "--add", "label=invalid", "title=(a");
    found = test_pattern_contains(output, "\x07invalid regex pattern '(a' for key 'title'\n");
    TEST_CHECK(found, true);
    TEST_CHECK(buf_len(g_window_manager.rules), rule_count);
    free(output);

    // Patterns compile without submatches and match as before.
    free(TEST_PATTERN_RUN("rule", "--add", "label=simple", "app=^(Fin|Saf)[a-z]+$", "title!=^Prefs?$"));
    TEST_CHECK(buf_len(g_window_manager.rules), rule_count + 1);
    struct rule *rule = buf_len(g_window_manager.rules) > rule_count ? &g_window_manager.rules[rule_count] : NULL;
    if (rule) {
        TEST_CHECK(regex_match(rule_check_flag(rule, RULE_APP_VALID), &rule->app_regex, "Finder"), REGEX_MATCH_YES);
        TEST_CHECK(regex_match(rule_check_flag(rule, RULE_APP_VALID), &rule->app_regex, "Safari"), REGEX_MATCH_YES);
        TEST_CHECK(regex_match(rule_check_flag(rule, RULE_APP_VALID), &rule->app_regex, "Mail"), REGEX_MATCH_NO);
        TEST_CHECK(regex_match(rule_check_flag(rule, RULE_TITLE_VALID), &rule->title_regex, "Pref"), REGEX_MATCH_YES);
    }
    free(TEST_PATTERN_RUN("rule", "--remove", "simple"));
    TEST_CHECK(buf_len(g_window_manager.rules), rule_count);

    if (!saved_rules) {
        buf_free(g_window_manager.rules);
        g_window_manager.rules = NULL;
    }
    if (!saved_signals) {
        buf_free(g_signal_event[SIGNAL_WINDOW_CREATED]);
        g_signal_event[SIGNAL_WINDOW_CREATED] = NULL;
    }
});

#undef TEST_PATTERN_RUN
