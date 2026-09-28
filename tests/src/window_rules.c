// Window rule matching and managed-window lookup on synthetic windows.
// Rule effect stubs in tests.m record calls that would require SkyLight.

TEST_FUNC(window_rule_matching_and_effects,
{
    struct application application = { .name = "Editor" };
    struct window window = { .id = 301, .application = &application };
    struct space_manager manager = {0};
    struct window_manager window_manager = {0};
    struct rule rule = {0};
    rule_set_flag(&rule, RULE_APP_VALID);
    rule_set_flag(&rule, RULE_TITLE_VALID);
    TEST_CHECK(regcomp(&rule.app_regex, "^Editor$", REG_EXTENDED | REG_NOSUB), 0);
    TEST_CHECK(regcomp(&rule.title_regex, "^Draft", REG_EXTENDED | REG_NOSUB), 0);

    TEST_CHECK(window_manager_rule_matches_window(&rule, &window, "Draft 1", "AXWindow", "AXStandardWindow"), true);
    TEST_CHECK(window_manager_rule_matches_window(&rule, &window, "Final", "AXWindow", "AXStandardWindow"), false);
    application.name = "Browser";
    TEST_CHECK(window_manager_rule_matches_window(&rule, &window, "Draft 1", "AXWindow", "AXStandardWindow"), false);

    application.name = "Editor";
    rule_set_flag(&rule, RULE_TITLE_EXCLUDE);
    TEST_CHECK(window_manager_rule_matches_window(&rule, &window, "Draft 1", "AXWindow", "AXStandardWindow"), false);
    TEST_CHECK(window_manager_rule_matches_window(&rule, &window, "Final", "AXWindow", "AXStandardWindow"), true);
    rule_clear_flag(&rule, RULE_TITLE_EXCLUDE);

    test_rule_floating_calls = 0;
    test_rule_sticky_calls = 0;
    rule.effects.manage = RULE_PROP_ON;
    rule.effects.sticky = RULE_PROP_ON;
    rule.effects.mff = RULE_PROP_ON;
    buf_push(window_manager.rules, rule);

    window_manager_apply_manage_rules_to_window(&manager, &window_manager, &window,
                                                 "Draft 1", "AXWindow", "AXStandardWindow", false);
    TEST_CHECK(window_check_rule_flag(&window, WINDOW_RULE_MANAGED), true);
    TEST_CHECK(test_rule_floating_calls, 1);
    TEST_CHECK(test_rule_last_float, false);

    window_manager_apply_rules_to_window(&manager, &window_manager, &window,
                                          "Draft 1", "AXWindow", "AXStandardWindow", false);
    TEST_CHECK(test_rule_sticky_calls, 1);
    TEST_CHECK(test_rule_last_sticky, true);
    TEST_CHECK(window_check_rule_flag(&window, WINDOW_RULE_MFF), true);
    TEST_CHECK(window_check_rule_flag(&window, WINDOW_RULE_MFF_VALUE), true);

    rule.effects.manage = RULE_PROP_OFF;
    rule.effects.sticky = RULE_PROP_OFF;
    window_manager_apply_manage_rule_effects_to_window(&manager, &window_manager, &window, &rule.effects);
    window_manager_apply_rule_effects_to_window(&manager, &window_manager, &window, &rule.effects);
    TEST_CHECK(window_check_rule_flag(&window, WINDOW_RULE_MANAGED), false);
    TEST_CHECK(test_rule_floating_calls, 2);
    TEST_CHECK(test_rule_last_float, true);
    TEST_CHECK(test_rule_sticky_calls, 2);
    TEST_CHECK(test_rule_last_sticky, false);

    buf_free(window_manager.rules);
    regfree(&rule.app_regex);
    regfree(&rule.title_regex);
});

TEST_FUNC(window_tree_lookup_and_direction,
{
    struct window_manager manager = {0};
    struct window first = { .id = 401 };
    struct window second = { .id = 402 };
    struct window third = { .id = 403 };
    struct view view = {0};
    struct window_node root = { .area = { 0, 0, 100, 100 }, .split = SPLIT_Y };
    struct window_node west = { .parent = &root, .area = { 0, 0, 50, 100 }, .window_list = { 401 }, .window_order = { 401 }, .window_count = 1 };
    struct window_node east = { .parent = &root, .area = { 50, 0, 50, 100 }, .split = SPLIT_X };
    struct window_node northeast = { .parent = &east, .area = { 50, 0, 50, 50 }, .window_list = { 402 }, .window_order = { 402 }, .window_count = 1 };
    struct window_node southeast = { .parent = &east, .area = { 50, 50, 50, 50 }, .window_list = { 403 }, .window_order = { 403 }, .window_count = 1 };
    root.left = &west;
    root.right = &east;
    east.left = &northeast;
    east.right = &southeast;
    view.root = &root;
    view.sid = 51;

    table_init(&manager.window, 7, hash_wm, compare_wm);
    table_init(&manager.managed_window, 7, hash_wm, compare_wm);
    table_add(&manager.window, &first.id, &first);
    table_add(&manager.window, &second.id, &second);
    table_add(&manager.window, &third.id, &third);
    table_add(&manager.managed_window, &first.id, &view);
    table_add(&manager.managed_window, &second.id, &view);
    table_add(&manager.managed_window, &third.id, &view);

    uint32_t order[] = { 403, 401, 402 };
    TEST_CHECK(window_manager_find_rank_of_window_in_list(401, order, 3), 1);
    TEST_CHECK(window_manager_find_rank_of_window_in_list(999, order, 3), INT_MAX);
    TEST_CHECK(window_manager_find_sibling_for_managed_window(&manager, &second) == &third, true);
    TEST_CHECK(window_manager_find_first_nephew_for_managed_window(&manager, &first) == &second, true);
    TEST_CHECK(window_manager_find_second_nephew_for_managed_window(&manager, &first) == &third, true);
    TEST_CHECK(window_manager_find_uncle_for_managed_window(&manager, &second) == &first, true);

    test_direction_window_order[0] = 403;
    test_direction_window_order[1] = 402;
    test_direction_window_order[2] = 401;
    test_direction_window_count = 3;
    TEST_CHECK(window_manager_find_closest_managed_window_in_direction(&manager, &first, DIR_EAST) == &third, true);
    TEST_CHECK(window_manager_find_closest_managed_window_in_direction(&manager, &first, DIR_WEST) == NULL, true);

    test_direction_window_count = 0;
    table_free(&manager.managed_window);
    table_free(&manager.window);
});
