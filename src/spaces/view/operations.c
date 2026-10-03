// Window insertion, removal, stacking and lookup within a view.
// Runs on the event-loop thread.

struct window_node *view_find_window_node(struct view *view, uint32_t window_id)
{
    for (struct window_node *node = window_node_find_first_leaf(view->root); node; node = window_node_find_next_leaf(node)) {
        if (window_node_contains_window(node, window_id)) return node;
    }

    return NULL;
}

struct window_node *view_remove_window_node(struct view *view, struct window *window)
{
    struct window_node *node = view_find_window_node(view, window->id);
    if (!node) return NULL;

    if (node->window_count > 1) {
        bool removed_entry = false;
        bool removed_order = false;

        for (int i = 0; i < node->window_count; ++i) {
            if (!removed_entry && node->window_list[i] == window->id) {
                memmove(node->window_list + i, node->window_list + i + 1, sizeof(uint32_t) * (node->window_count - i - 1));
                removed_entry = true;
            }

            if (!removed_order && node->window_order[i] == window->id) {
                memmove(node->window_order + i, node->window_order + i + 1, sizeof(uint32_t) * (node->window_count - i - 1));
                removed_order = true;
            }
        }

        assert(removed_entry);
        assert(removed_order);
        --node->window_count;

        if (view->insertion_point == window->id) {
            view->insertion_point = node->window_order[0];
        }

        return NULL;
    }

    if (node == view->root) {
        view->insertion_point = 0;
        insert_feedback_destroy(node);
        memset(node, 0, sizeof(struct window_node));
        view_update(view);
        return NULL;
    }

    struct window_node *parent = node->parent;
    struct window_node *child  = window_node_is_right_child(node)
                               ? parent->left
                               : parent->right;


    memcpy(parent->window_list, child->window_list, sizeof(uint32_t) * child->window_count);
    memcpy(parent->window_order, child->window_order, sizeof(uint32_t) * child->window_count);
    parent->window_count = child->window_count;

    parent->left      = NULL;
    parent->right     = NULL;
    parent->zoom      = !g_space_manager.window_zoom_persist
                      ? NULL
                      : !child->zoom
                      ? NULL
                      : child->zoom == parent
                      ? parent->parent
                      : view->root;

    if (child->insert_dir) {
        parent->feedback_window = child->feedback_window;
        parent->insert_dir      = child->insert_dir;
        parent->split           = child->split;
        parent->child           = child->child;
        insert_feedback_show(parent);
    }

    if (window_node_is_intermediate(child) && !window_node_is_leaf(child)) {
        parent->left          = child->left;
        parent->left->parent  = parent;
        parent->left->zoom    = !g_space_manager.window_zoom_persist
                              ? NULL
                              : !child->left->zoom
                              ? NULL
                              : child->left->zoom == child
                              ? parent
                              : view->root;

        parent->right         = child->right;
        parent->right->parent = parent;
        parent->right->zoom   = !g_space_manager.window_zoom_persist
                              ? NULL
                              : !child->right->zoom
                              ? NULL
                              : child->right->zoom == child
                              ? parent
                              : view->root;

        if (!g_space_manager.window_zoom_persist) {
            window_node_clear_zoom(parent);
        }

        window_node_update(view, parent);
    }

    insert_feedback_destroy(node);
    free(child);
    free(node);

    if (view->auto_balance != SPLIT_NONE) {
        window_node_balance(view->root, view->auto_balance);
        view_update(view);
        return view->root;
    }

    return parent;
}

bool view_stack_window_node(struct window_node *node, struct window *window)
{
    if (node->window_count < 0 || node->window_count >= NODE_MAX_WINDOW_COUNT) return false;

    int insert_index = node->window_count;

    for (int i = 0; i < node->window_count; ++i) {
        if (node->window_list[i] == node->window_order[0]) {
            insert_index = i+1;
            break;
        }
    }

    if (insert_index < node->window_count) {
        memmove(node->window_list + insert_index + 1, node->window_list + insert_index, sizeof(uint32_t) * (node->window_count - insert_index));
    }

    node->window_list[insert_index] = window->id;
    memmove(node->window_order + 1, node->window_order, sizeof(uint32_t) * node->window_count);
    node->window_order[0] = window->id;
    ++node->window_count;
    return true;
}

struct window_node *view_add_window_node_with_insertion_point(struct view *view, struct window *window, uint32_t insertion_point)
{
    if (!window_node_is_occupied(view->root) &&
        window_node_is_leaf(view->root)) {
        view->root->window_list[0] = window->id;
        view->root->window_order[0] = window->id;
        view->root->window_count = 1;
        return view->root;
    } else if (view->layout == VIEW_BSP) {
        uint32_t prev_insertion_point = 0;
        struct window_node *leaf = NULL;

        if (insertion_point) {
            prev_insertion_point = view->insertion_point;
            view->insertion_point = insertion_point;
        }

        if (view->insertion_point) {
            leaf = view_find_window_node(view, view->insertion_point);
            view->insertion_point = prev_insertion_point;

            if (leaf) {
                bool do_stack = leaf->insert_dir == STACK;

                leaf->insert_dir = 0;
                insert_feedback_destroy(leaf);

                if (do_stack && view_stack_window_node(leaf, window)) {
                    return leaf;
                }
            }
        }

        if (!leaf) {
            if (g_space_manager.window_insertion_point == INSERT_FOCUSED) {
                leaf = view_find_window_node(view, g_window_manager.focused_window_id);
            } else if (g_space_manager.window_insertion_point == INSERT_FIRST) {
                leaf = window_node_find_first_leaf(view->root);
            } else if (g_space_manager.window_insertion_point == INSERT_LAST) {
                leaf = window_node_find_last_leaf(view->root);
            }

            if (!leaf) leaf = view_find_min_depth_leaf_node(view->root);
        }

        window_node_split(view, leaf, window);

        if (view->auto_balance != SPLIT_NONE) {
            window_node_balance(view->root, view->auto_balance);
            view_update(view);
            return view->root;
        }

        return leaf;
    } else if (view->layout == VIEW_STACK) {
        return view_stack_window_node(view->root, window) ? view->root : NULL;
    }

    return NULL;
}

struct window_node *view_add_window_node(struct view *view, struct window *window)
{
    return view_add_window_node_with_insertion_point(view, window, 0);
}

uint32_t *view_find_window_list(struct view *view, int *window_count)
{
    *window_count = 0;

    int capacity = 13;
    uint32_t *window_list = ts_alloc_list(uint32_t, capacity);

    for (struct window_node *node = window_node_find_first_leaf(view->root); node; node = window_node_find_next_leaf(node)) {
        int required = *window_count + node->window_count;
        if (required > capacity) {
            int old_capacity = capacity;
            while (capacity < required) capacity *= 2;
            ts_expand(window_list,
                      sizeof(uint32_t) * old_capacity,
                      sizeof(uint32_t) * (capacity - old_capacity));
        }

        for (int i = 0; i < node->window_count; ++i) {
            window_list[(*window_count)++] = node->window_list[i];
        }
    }

    return window_list;
}
