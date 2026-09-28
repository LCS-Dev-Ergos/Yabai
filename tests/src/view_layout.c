// Space layout commands against a synthetic view and deterministic display.
// The view update boundary in tests.m avoids SkyLight and WindowServer calls.

static void test_view_layout_init(struct space_manager *manager, struct view *view,
                                  struct window_node *root, uint64_t sid)
{
    memset(manager, 0, sizeof(*manager));
    memset(view, 0, sizeof(*view));
    memset(root, 0, sizeof(*root));

    table_init(&manager->view, 7, hash_view, compare_view);
    view->sid = sid;
    view->root = root;
    view->layout = VIEW_BSP;
    view->split_type = SPLIT_Y;
    view_set_flag(view, VIEW_ENABLE_PADDING);
    view_set_flag(view, VIEW_ENABLE_GAP);
    table_add(&manager->view, &sid, view);
}

TEST_FUNC(view_layout_padding_and_gap_commands,
{
    struct space_manager manager;
    struct view view;
    struct window_node root;
    struct window_node first = { .parent = &root, .window_list = { 1 }, .window_count = 1 };
    struct window_node second = { .parent = &root, .window_list = { 2 }, .window_count = 1 };
    test_view_layout_init(&manager, &view, &root, 42);
    root.split = SPLIT_Y;
    root.ratio = 0.5f;
    root.left = &first;
    root.right = &second;

    TEST_CHECK(space_manager_set_padding_for_space(&manager, 42, TYPE_ABS, 10, 20, 30, 40), true);
    TEST_CHECK((int)root.area.x, 30);
    TEST_CHECK((int)root.area.y, 10);
    TEST_CHECK((int)root.area.w, 930);
    TEST_CHECK((int)root.area.h, 570);

    TEST_CHECK(space_manager_set_gap_for_space(&manager, 42, TYPE_ABS, 20), true);
    TEST_CHECK((int)first.area.w, 455);
    TEST_CHECK((int)second.area.x, 505);
    TEST_CHECK((int)second.area.w, 455);

    TEST_CHECK(space_manager_set_gap_for_space(&manager, 42, TYPE_REL, -5), true);
    TEST_CHECK(view.window_gap, 15);
    TEST_CHECK((int)first.area.w, 457);
    TEST_CHECK((int)second.area.x, 503);

    TEST_CHECK(space_manager_toggle_gap_for_space(&manager, 42), true);
    TEST_CHECK((int)first.area.w, 465);
    TEST_CHECK((int)second.area.x, 495);

    TEST_CHECK(space_manager_toggle_padding_for_space(&manager, 42), true);
    TEST_CHECK((int)root.area.x, 0);
    TEST_CHECK((int)root.area.w, 1000);

    TEST_CHECK(space_manager_toggle_padding_for_space(&manager, 42), true);
    TEST_CHECK(space_manager_set_padding_for_space(&manager, 42, TYPE_REL, -20, 5, -40, 10), true);
    TEST_CHECK(view.top_padding, 0);
    TEST_CHECK(view.bottom_padding, 25);
    TEST_CHECK(view.left_padding, 0);
    TEST_CHECK(view.right_padding, 50);
    TEST_CHECK((int)root.area.w, 950);
    TEST_CHECK((int)root.area.h, 575);

    view.layout = VIEW_FLOAT;
    TEST_CHECK(space_manager_set_gap_for_space(&manager, 42, TYPE_ABS, 99), false);
    TEST_CHECK(space_manager_set_padding_for_space(&manager, 42, TYPE_ABS, 1, 1, 1, 1), false);
    TEST_CHECK(view.window_gap, 15);
    TEST_CHECK(view.right_padding, 50);
    table_free(&manager.view);
});

TEST_FUNC(view_layout_tree_commands,
{
    struct space_manager saved_manager = g_space_manager;
    struct space_manager manager;
    struct view view;
    struct window_node root;
    struct window_node first = { .parent = &root, .window_list = { 1 }, .window_count = 1 };
    struct window_node branch = { .parent = &root, .split = SPLIT_Y, .ratio = 0.8f };
    struct window_node second = { .parent = &branch, .window_list = { 2 }, .window_count = 1 };
    struct window_node third = { .parent = &branch, .window_list = { 3 }, .window_count = 1 };
    test_view_layout_init(&manager, &view, &root, 43);
    root.split = SPLIT_Y;
    root.ratio = 0.8f;
    root.left = &first;
    root.right = &branch;
    branch.left = &second;
    branch.right = &third;
    g_space_manager.split_ratio = 0.6f;

    TEST_CHECK(space_manager_balance_space(&manager, 43, SPLIT_Y), true);
    TEST_CHECK(fabsf(root.ratio - 1.0f / 3.0f) < 0.001f, true);
    TEST_CHECK(branch.ratio == 0.5f, true);

    TEST_CHECK(space_manager_equalize_space(&manager, 43, SPLIT_Y), true);
    TEST_CHECK(root.ratio == 0.6f, true);
    TEST_CHECK(branch.ratio == 0.6f, true);

    TEST_CHECK(space_manager_rotate_space(&manager, 43, 90), true);
    TEST_CHECK(root.split, SPLIT_X);
    TEST_CHECK(window_node_find_first_leaf(&root)->window_list[0], 3);

    TEST_CHECK(space_manager_mirror_space(&manager, 43, SPLIT_X), true);
    TEST_CHECK(window_node_find_first_leaf(&root)->window_list[0], 1);

    view.layout = VIEW_FLOAT;
    TEST_CHECK(space_manager_rotate_space(&manager, 43, 180), false);
    TEST_CHECK(space_manager_mirror_space(&manager, 43, SPLIT_Y), false);
    TEST_CHECK(space_manager_balance_space(&manager, 43, SPLIT_Y), false);
    TEST_CHECK(space_manager_equalize_space(&manager, 43, SPLIT_Y), false);

    table_free(&manager.view);
    g_space_manager = saved_manager;
});

TEST_FUNC(view_layout_type_command,
{
    struct space_manager manager;
    struct view view;
    struct window_node root;
    test_view_layout_init(&manager, &view, &root, 44);

    space_manager_set_layout_for_space(&manager, 44, VIEW_FLOAT);
    TEST_CHECK(view.layout, VIEW_FLOAT);
    TEST_CHECK(window_node_is_leaf(&root), true);
    TEST_CHECK(view_check_flag(&view, VIEW_IS_VALID) != 0, true);

    table_free(&manager.view);
});
