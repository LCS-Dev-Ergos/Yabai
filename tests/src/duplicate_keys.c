// A key given twice to rule --add or signal --add keeps its last value, as it
// always has, and releases the first one: its text, its compiled pattern and,
// for app and title, whether it was an exclusion.

#include <malloc/malloc.h>

// Sanitizers hold freed memory back, in quarantine or their own allocator, and
// the default zone counts it as in use.
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define TEST_DUPLICATE_SANITIZED 1
#endif
#endif

#define TEST_DUPLICATE_RUN(...) free(test_command_run((const char *[]) { __VA_ARGS__ }, \
                                     (int)(sizeof((const char *[]) { __VA_ARGS__ }) / sizeof(char *))))

#ifndef TEST_DUPLICATE_SANITIZED
static size_t test_duplicate_bytes_in_use(void)
{
    malloc_statistics_t statistics = {0};
    malloc_zone_statistics(NULL, &statistics);
    return statistics.size_in_use;
}
#endif

TEST_FUNC(duplicate_rule_and_signal_keys,
{
    struct rule *saved_rules = g_window_manager.rules;
    struct signal *saved_signals = g_signal_event[SIGNAL_WINDOW_FOCUSED];
    int rule_count = buf_len(g_window_manager.rules);
    int signal_count = buf_len(g_signal_event[SIGNAL_WINDOW_FOCUSED]);

    TEST_DUPLICATE_RUN("rule", "--add", "label=first", "label=twice", "app!=^Mail$", "app=^Editor$",
                       "title=one", "title=two", "role=a", "role=AXWindow", "subrole=b", "subrole=AXStandardWindow",
                       "scratchpad=x", "scratchpad=pad");
    TEST_CHECK(buf_len(g_window_manager.rules), rule_count + 1);
    struct rule *rule = buf_len(g_window_manager.rules) > rule_count ? &g_window_manager.rules[rule_count] : NULL;
    if (rule) {
        TEST_CHECK(strcmp(rule->label, "twice"), 0);
        TEST_CHECK(strcmp(rule->app, "^Editor$"), 0);
        TEST_CHECK(strcmp(rule->title, "two"), 0);
        TEST_CHECK(strcmp(rule->role, "AXWindow"), 0);
        TEST_CHECK(strcmp(rule->subrole, "AXStandardWindow"), 0);
        TEST_CHECK(strcmp(rule->effects.scratchpad, "pad"), 0);
        TEST_CHECK(rule_check_flag(rule, RULE_APP_EXCLUDE), false);
        TEST_CHECK(rule_check_flag(rule, RULE_APP_VALID), true);
        TEST_CHECK(regexec(&rule->app_regex, "Editor", 0, NULL, 0), 0);
        TEST_CHECK(regexec(&rule->app_regex, "Mail", 0, NULL, 0) == 0, false);
    }
    TEST_DUPLICATE_RUN("rule", "--remove", "twice");
    TEST_CHECK(buf_len(g_window_manager.rules), rule_count);

    // An exclusion given last stays one.
    TEST_DUPLICATE_RUN("rule", "--add", "label=excluded", "title=one", "title!=two", "app=x");
    rule = buf_len(g_window_manager.rules) > rule_count ? &g_window_manager.rules[rule_count] : NULL;
    if (rule) TEST_CHECK(rule_check_flag(rule, RULE_TITLE_EXCLUDE), true);
    TEST_DUPLICATE_RUN("rule", "--remove", "excluded");

    TEST_DUPLICATE_RUN("signal", "--add", "label=first", "label=twice", "event=window_focused",
                       "app!=^Mail$", "app=^Editor$", "title=one", "title=two", "action=false", "action=true");
    TEST_CHECK(buf_len(g_signal_event[SIGNAL_WINDOW_FOCUSED]), signal_count + 1);
    struct signal *signal = buf_len(g_signal_event[SIGNAL_WINDOW_FOCUSED]) > signal_count
                          ? &g_signal_event[SIGNAL_WINDOW_FOCUSED][signal_count] : NULL;
    if (signal) {
        TEST_CHECK(strcmp(signal->label, "twice"), 0);
        TEST_CHECK(strcmp(signal->app, "^Editor$"), 0);
        TEST_CHECK(strcmp(signal->title, "two"), 0);
        TEST_CHECK(strcmp(signal->command, "true"), 0);
        TEST_CHECK(signal->app_regex_exclude, false);
        TEST_CHECK(signal->app_regex_valid, true);
        TEST_CHECK(regexec(&signal->app_regex, "Editor", 0, NULL, 0), 0);
    }
    TEST_DUPLICATE_RUN("signal", "--remove", "twice");
    TEST_CHECK(buf_len(g_signal_event[SIGNAL_WINDOW_FOCUSED]), signal_count);

    // Repeating such requests holds no memory once the rule and signal are
    // removed; each first value used to stay allocated, pattern included.
    // Only a build without sanitizers can measure this.
#ifndef TEST_DUPLICATE_SANITIZED
    TEST_DUPLICATE_RUN("rule", "--add", "label=warm", "app=a", "app=b");
    TEST_DUPLICATE_RUN("rule", "--remove", "warm");
    TEST_DUPLICATE_RUN("signal", "--add", "label=warm", "event=window_focused", "app=a", "app=b", "action=true");
    TEST_DUPLICATE_RUN("signal", "--remove", "warm");

    size_t before = test_duplicate_bytes_in_use();
    for (int i = 0; i < 200; ++i) {
        TEST_DUPLICATE_RUN("rule", "--add", "label=repeat", "app=(Editor|Browser)+", "app=(Mail|Notes)+",
                           "title=a", "title=b", "scratchpad=x", "scratchpad=y");
        TEST_DUPLICATE_RUN("rule", "--remove", "repeat");
        TEST_DUPLICATE_RUN("signal", "--add", "label=repeat", "event=window_focused", "app=(Editor|Browser)+",
                           "app=(Mail|Notes)+", "action=false", "action=true");
        TEST_DUPLICATE_RUN("signal", "--remove", "repeat");
    }
    size_t after = test_duplicate_bytes_in_use();
    if (after > before + 64 * 1024) {
        printf("                   %zu bytes held after 200 repeats\n", after - before);
        result = false;
    }
#endif

    if (!saved_rules) {
        buf_free(g_window_manager.rules);
        g_window_manager.rules = NULL;
    }
    if (!saved_signals) {
        buf_free(g_signal_event[SIGNAL_WINDOW_FOCUSED]);
        g_signal_event[SIGNAL_WINDOW_FOCUSED] = NULL;
    }
});

#undef TEST_DUPLICATE_RUN
#undef TEST_DUPLICATE_SANITIZED
