// BSP insertion, traversal, geometry and transformations on synthetic views.
// Runs inside yabai_tests without a WindowServer connection.

static void test_view_tree_init(struct view *view, struct window_node *root)
{
    memset(view, 0, sizeof(*view));
    memset(root, 0, sizeof(*root));

    view->layout = VIEW_BSP;
    view->root = root;
    view->split_type = SPLIT_Y;
    view->window_gap = 20;
    view_set_flag(view, VIEW_ENABLE_GAP);
    root->area = (struct area) { 0, 0, 1000, 600 };
}

static void test_view_tree_free_children(struct window_node *node)
{
    if (node->left) {
        test_view_tree_free_children(node->left);
        free(node->left);
    }

    if (node->right) {
        test_view_tree_free_children(node->right);
        free(node->right);
    }
}

TEST_FUNC(view_tree_insert_and_remove,
{
    struct space_manager saved_manager = g_space_manager;
    struct view view;
    struct window_node root;
    struct window first = { .id = 101 };
    struct window second = { .id = 102 };
    struct window third = { .id = 103 };

    g_space_manager.window_placement = CHILD_SECOND;
    g_space_manager.window_insertion_point = INSERT_LAST;
    g_space_manager.split_ratio = 0.5f;
    g_space_manager.window_zoom_persist = false;
    test_view_tree_init(&view, &root);

    TEST_CHECK(view_add_window_node(&view, &first) == &root, true);
    TEST_CHECK(window_node_is_leaf(&root), true);
    TEST_CHECK(root.window_list[0], 101);

    view_add_window_node(&view, &second);
    TEST_CHECK(window_node_is_leaf(&root), false);
    TEST_CHECK(window_node_is_left_child(root.left), true);
    TEST_CHECK(window_node_is_left_child(root.right), false);
    TEST_CHECK(root.left->window_list[0], 101);
    TEST_CHECK(root.right->window_list[0], 102);
    TEST_CHECK((int)root.left->area.w, 490);
    TEST_CHECK((int)root.right->area.x, 510);

    view_add_window_node_with_insertion_point(&view, &third, 101);
    TEST_CHECK(root.left->left->window_list[0], 101);
    TEST_CHECK(root.left->right->window_list[0], 103);
    TEST_CHECK(view_find_window_node(&view, 103) == root.left->right, true);
    TEST_CHECK(window_node_find_first_leaf(&root)->window_list[0], 101);
    TEST_CHECK(window_node_find_last_leaf(&root)->window_list[0], 102);

    view_remove_window_node(&view, &third);
    TEST_CHECK(window_node_is_leaf(root.left), true);
    TEST_CHECK(root.left->window_list[0], 101);
    TEST_CHECK(view_find_window_node(&view, 103) == NULL, true);

    test_view_tree_free_children(&root);
    g_space_manager = saved_manager;
});

TEST_FUNC(view_tree_split_preferences,
{
    struct space_manager saved_manager = g_space_manager;
    struct view view;
    struct window_node root;
    struct window first = { .id = 201 };
    struct window second = { .id = 202 };
    struct window third = { .id = 203 };

    g_space_manager.window_placement = CHILD_FIRST;
    g_space_manager.window_insertion_point = INSERT_FIRST;
    g_space_manager.split_ratio = 0.25f;
    g_space_manager.window_zoom_persist = false;
    test_view_tree_init(&view, &root);
    view.split_type = SPLIT_X;

    view_add_window_node(&view, &first);
    view_add_window_node(&view, &second);
    TEST_CHECK(root.left->window_list[0], 202);
    TEST_CHECK(root.right->window_list[0], 201);
    TEST_CHECK(root.split, SPLIT_X);
    TEST_CHECK((int)root.left->area.h, 145);
    TEST_CHECK((int)root.right->area.y, 165);

    g_space_manager.window_placement = CHILD_SECOND;
    view_add_window_node(&view, &third);
    TEST_CHECK(root.left->left->window_list[0], 202);
    TEST_CHECK(root.left->right->window_list[0], 203);

    test_view_tree_free_children(&root);
    g_space_manager = saved_manager;
});

TEST_FUNC(view_tree_rotation_and_mirror,
{
    struct window_node root = { .split = SPLIT_Y, .ratio = 0.25f };
    struct window_node first = { .parent = &root, .window_list = { 1 }, .window_count = 1 };
    struct window_node second = { .parent = &root, .window_list = { 2 }, .window_count = 1 };
    root.left = &first;
    root.right = &second;

    window_node_rotate(&root, 90);
    TEST_CHECK(root.split, SPLIT_X);
    TEST_CHECK(root.left->window_list[0], 2);
    TEST_CHECK(root.ratio == 0.75f, true);

    root.split = SPLIT_Y;
    root.ratio = 0.25f;
    root.left = &first;
    root.right = &second;
    window_node_rotate(&root, 180);
    TEST_CHECK(root.split, SPLIT_Y);
    TEST_CHECK(root.left->window_list[0], 2);
    TEST_CHECK(root.ratio == 0.75f, true);

    root.split = SPLIT_Y;
    root.ratio = 0.25f;
    root.left = &first;
    root.right = &second;
    window_node_rotate(&root, 270);
    TEST_CHECK(root.split, SPLIT_X);
    TEST_CHECK(root.left->window_list[0], 1);
    TEST_CHECK(root.ratio == 0.25f, true);

    root.split = SPLIT_Y;
    root.left = &first;
    root.right = &second;
    window_node_mirror(&root, SPLIT_Y);
    TEST_CHECK(root.left->window_list[0], 2);
    window_node_mirror(&root, SPLIT_X);
    TEST_CHECK(root.left->window_list[0], 2);
});

TEST_FUNC(view_tree_balance_and_equalize,
{
    struct space_manager saved_manager = g_space_manager;
    struct window_node root = { .split = SPLIT_Y, .ratio = 0.8f };
    struct window_node first = { .parent = &root, .window_count = 1 };
    struct window_node branch = { .parent = &root, .split = SPLIT_Y, .ratio = 0.8f };
    struct window_node second = { .parent = &branch, .window_count = 1 };
    struct window_node third = { .parent = &branch, .window_count = 1 };
    root.left = &first;
    root.right = &branch;
    branch.left = &second;
    branch.right = &third;

    window_node_balance(&root, SPLIT_Y);
    TEST_CHECK(fabsf(root.ratio - 1.0f / 3.0f) < 0.001f, true);
    TEST_CHECK(branch.ratio == 0.5f, true);

    g_space_manager.split_ratio = 0.6f;
    window_node_equalize(&root, SPLIT_Y);
    TEST_CHECK(root.ratio == 0.6f, true);
    TEST_CHECK(branch.ratio == 0.6f, true);

    window_node_equalize(&root, SPLIT_X);
    TEST_CHECK(root.ratio == 0.6f, true);
    g_space_manager = saved_manager;
});

TEST_FUNC(view_tree_fence_and_traversal,
{
    struct window_node root = { .area = { 0, 0, 100, 100 }, .split = SPLIT_Y };
    struct window_node west = { .parent = &root, .area = { 0, 0, 50, 100 }, .window_list = { 1 }, .window_count = 1 };
    struct window_node east = { .parent = &root, .area = { 50, 0, 50, 100 }, .window_list = { 2 }, .window_count = 1 };
    root.left = &west;
    root.right = &east;

    TEST_CHECK(window_node_fence(&west, DIR_EAST) == &root, true);
    TEST_CHECK(window_node_fence(&east, DIR_WEST) == &root, true);
    TEST_CHECK(window_node_fence(&west, DIR_WEST) == NULL, true);
    TEST_CHECK(window_node_find_next_leaf(&west) == &east, true);
    TEST_CHECK(window_node_find_prev_leaf(&east) == &west, true);
    TEST_CHECK(window_node_find_prev_leaf(&west) == NULL, true);

    root.split = SPLIT_X;
    west.area = (struct area) { 0, 0, 100, 50 };
    east.area = (struct area) { 0, 50, 100, 50 };
    TEST_CHECK(window_node_fence(&west, DIR_SOUTH) == &root, true);
    TEST_CHECK(window_node_fence(&east, DIR_NORTH) == &root, true);
});
