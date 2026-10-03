// Regression tests for fixed-capacity collections at security-sensitive
// window, layout and image-processing boundaries.

static void test_security_bounds_init_temp_storage(void)
{
    if (!g_temp_storage.memory) {
        bool initialized = ts_init(MEGABYTES(1));
        assert(initialized);
    }
}

TEST_FUNC(window_notifications_capacity,
{
    uint32_t *window_list = NULL;
    int window_count = 0;
    int capacity = 0;

    for (uint32_t i = 0; i < 2049; ++i) {
        TEST_CHECK(window_notification_list_append(&window_list, &window_count, &capacity, i + 1), true);
    }

    TEST_CHECK(window_count, 2049);
    TEST_CHECK(capacity >= window_count, true);
    TEST_CHECK(window_list[0], 1);
    TEST_CHECK(window_list[1023], 1024);
    TEST_CHECK(window_list[1024], 1025);
    TEST_CHECK(window_list[2048], 2049);

    free(window_list);
});

TEST_FUNC(view_stack_capacity,
{
    test_security_bounds_init_temp_storage();

    struct window_node node = {0};
    for (int i = 0; i < NODE_MAX_WINDOW_COUNT - 1; ++i) {
        node.window_list[i] = (uint32_t) i + 1;
        node.window_order[i] = (uint32_t) i + 1;
    }
    node.window_count = NODE_MAX_WINDOW_COUNT - 1;

    struct window last = { .id = 1000 };
    TEST_CHECK(view_stack_window_node(&node, &last), true);
    TEST_CHECK(node.window_count, NODE_MAX_WINDOW_COUNT);
    TEST_CHECK(node.window_order[0], 1000);

    struct window_node snapshot = node;
    struct window overflow = { .id = 1001 };
    TEST_CHECK(view_stack_window_node(&node, &overflow), false);
    TEST_CHECK(memcmp(&node, &snapshot, sizeof(node)), 0);

    struct view stack_view = { .layout = VIEW_STACK, .root = &node };
    TEST_CHECK(view_add_window_node(&stack_view, &overflow) == NULL, true);
    TEST_CHECK(memcmp(&node, &snapshot, sizeof(node)), 0);

    ts_reset();
    int window_count = 0;
    uint32_t *window_list = view_find_window_list(&stack_view, &window_count);
    TEST_CHECK(window_count, NODE_MAX_WINDOW_COUNT);
    for (int i = 0; i < NODE_MAX_WINDOW_COUNT; ++i) {
        TEST_CHECK(window_list[i], node.window_list[i]);
    }
    ts_reset();
});

TEST_FUNC(view_stack_rejected_ingress,
{
    struct window_node stack_node = { .window_count = NODE_MAX_WINDOW_COUNT };
    struct view stack_view = { .layout = VIEW_STACK, .root = &stack_node };
    struct window overflow = { .id = 1001 };

    struct window_node src_root = {0};
    struct window_node src_leaf = { .parent = &src_root, .window_count = 1 };
    src_leaf.window_list[0] = overflow.id;
    src_leaf.window_order[0] = overflow.id;
    src_root.left = &src_leaf;
    struct view src_view = { .layout = VIEW_BSP, .root = &src_root };

    struct window destination = { .id = 1 };
    stack_node.window_list[0] = destination.id;
    stack_node.window_order[0] = destination.id;

    struct window_node stack_snapshot = stack_node;
    struct window_node src_snapshot = src_leaf;
    mouse_drop_action_warp(&g_window_manager, &src_view, &src_leaf, &overflow,
                           &stack_view, &stack_node, &destination, SPLIT_X, CHILD_SECOND);
    TEST_CHECK(memcmp(&stack_node, &stack_snapshot, sizeof(stack_node)), 0);
    TEST_CHECK(memcmp(&src_leaf, &src_snapshot, sizeof(src_leaf)), 0);

    struct table saved_managed_window = g_window_manager.managed_window;
    table_init(&g_window_manager.managed_window, 7, hash_wm, compare_wm);
    TEST_CHECK(application_batch_add_window(&stack_view, &overflow, destination.id), false);
    TEST_CHECK(window_manager_find_managed_window(&g_window_manager, &overflow) == NULL, true);
    TEST_CHECK(view_find_window_node(&stack_view, overflow.id) == NULL, true);
    TEST_CHECK(view_check_flag(&stack_view, VIEW_IS_DIRTY) != 0, false);
    TEST_CHECK(memcmp(&stack_node, &stack_snapshot, sizeof(stack_node)), 0);
    table_free(&g_window_manager.managed_window);
    g_window_manager.managed_window = saved_managed_window;
});

TEST_FUNC(view_tree_min_depth_growth,
{
    test_security_bounds_init_temp_storage();

    struct space_manager saved_manager = g_space_manager;
    uint32_t saved_focused_window_id = g_window_manager.focused_window_id;
    struct view view;
    struct window_node root;
    int window_total = 258;
    struct window *windows = calloc((size_t) window_total, sizeof(struct window));
    TEST_CHECK(windows != NULL, true);

    if (windows) {
        g_space_manager.window_placement = CHILD_SECOND;
        g_space_manager.window_insertion_point = INSERT_FOCUSED;
        g_space_manager.split_ratio = 0.5f;
        g_space_manager.window_zoom_persist = false;
        g_window_manager.focused_window_id = 0;
        test_view_tree_init(&view, &root);

        for (int i = 0; i < window_total; ++i) {
            windows[i].id = (uint32_t) i + 1;
            ts_reset();
            TEST_CHECK(view_add_window_node(&view, &windows[i]) != NULL, true);
        }

        int leaf_count = 0;
        for (struct window_node *leaf = window_node_find_first_leaf(&root);
             leaf;
             leaf = window_node_find_next_leaf(leaf)) {
            ++leaf_count;
        }
        TEST_CHECK(leaf_count, window_total);
        TEST_CHECK(view_find_window_node(&view, 130) != NULL, true);
        TEST_CHECK(view_find_window_node(&view, 258) != NULL, true);

        test_view_tree_free_children(&root);
        free(windows);
    }

    ts_reset();
    g_window_manager.focused_window_id = saved_focused_window_id;
    g_space_manager = saved_manager;
});

TEST_FUNC(cgimage_restore_alpha_tail,
{
    const uint32_t source = 0x80402010;
    const uint32_t expected = 0xff804020;
    const uint32_t guard = 0xdeadbeef;

    for (size_t count = 1; count <= 8; ++count) {
        uint32_t pixels[9];
        for (size_t i = 0; i < count; ++i) pixels[i] = source;
        for (size_t i = count; i < array_count(pixels); ++i) pixels[i] = guard;

        cgimage_restore_alpha_pixels(pixels, count);

        for (size_t i = 0; i < count; ++i) TEST_CHECK(pixels[i], expected);
        TEST_CHECK(pixels[count], guard);
    }

    uint32_t transparent[2] = { 0x00030201, guard };
    cgimage_restore_alpha_pixels(transparent, 1);
    TEST_CHECK(transparent[0], 0x00030201);
    TEST_CHECK(transparent[1], guard);
});
