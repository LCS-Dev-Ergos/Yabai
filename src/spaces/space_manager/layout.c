// Layout, padding, gap, tiling and split commands.
// Runs on the event-loop thread.

void space_manager_set_layout_for_space(struct space_manager *sm, uint64_t sid, enum view_type layout)
{
    struct view *view = space_manager_find_view(sm, sid);
    view->layout = layout;
    view_clear(view);

    if (view->layout != VIEW_FLOAT) {
        window_manager_validate_and_check_for_windows_on_space(sm, &g_window_manager, sid);
    }
}

bool space_manager_set_gap_for_space(struct space_manager *sm, uint64_t sid, int type, int gap)
{
    struct view *view = space_manager_find_view(sm, sid);
    if (view->layout == VIEW_FLOAT) return false;

    if (type == TYPE_ABS) {
        view->window_gap = gap;
    } else if (type == TYPE_REL) {
        view->window_gap = add_and_clamp_to_zero(view->window_gap, gap);
    }

    view_update(view);
    view_flush(view);

    return true;
}

bool space_manager_toggle_gap_for_space(struct space_manager *sm, uint64_t sid)
{
    struct view *view = space_manager_find_view(sm, sid);
    if (view->layout == VIEW_FLOAT) return false;

    if (view_check_flag(view, VIEW_ENABLE_GAP)) {
        view_clear_flag(view, VIEW_ENABLE_GAP);
    } else {
        view_set_flag(view, VIEW_ENABLE_GAP);
    }

    view_update(view);
    view_flush(view);

    return true;
}

void space_manager_toggle_mission_control(uint64_t sid)
{
    space_manager_focus_space(sid);
    CoreDockSendNotification(CFSTR("com.apple.expose.awake"), 0);
}

void space_manager_toggle_show_desktop(uint64_t sid)
{
    space_manager_focus_space(sid);
    CoreDockSendNotification(CFSTR("com.apple.showdesktop.awake"), 0);
}

void space_manager_set_layout_for_all_spaces(struct space_manager *sm, enum view_type layout)
{
    sm->layout = layout;
    table_for (struct view *view, sm->view, {
        if (!view_check_flag(view, VIEW_LAYOUT)) {
            if (space_is_user(view->sid)) {
                view->layout = layout;
                view_clear(view);

                if (view->layout != VIEW_FLOAT) {
                    window_manager_validate_and_check_for_windows_on_space(sm, &g_window_manager, view->sid);
                }
            }
        }
    })
}

void space_manager_set_window_gap_for_all_spaces(struct space_manager *sm, int window_gap)
{
    sm->window_gap = window_gap;
    table_for (struct view *view, sm->view, {
        if (!view_check_flag(view, VIEW_WINDOW_GAP)) {
            view->window_gap = window_gap;
            view_update(view);
            view_flush(view);
        }
    })
}

void space_manager_set_top_padding_for_all_spaces(struct space_manager *sm, int top_padding)
{
    sm->top_padding = top_padding;
    table_for (struct view *view, sm->view, {
        if (!view_check_flag(view, VIEW_TOP_PADDING)) {
            view->top_padding = top_padding;
            view_update(view);
            view_flush(view);
        }
    })
}

void space_manager_set_bottom_padding_for_all_spaces(struct space_manager *sm, int bottom_padding)
{
    sm->bottom_padding = bottom_padding;
    table_for (struct view *view, sm->view, {
        if (!view_check_flag(view, VIEW_BOTTOM_PADDING)) {
            view->bottom_padding = bottom_padding;
            view_update(view);
            view_flush(view);
        }
    })
}

void space_manager_set_left_padding_for_all_spaces(struct space_manager *sm, int left_padding)
{
    sm->left_padding = left_padding;
    table_for (struct view *view, sm->view, {
        if (!view_check_flag(view, VIEW_LEFT_PADDING)) {
            view->left_padding = left_padding;
            view_update(view);
            view_flush(view);
        }
    })
}

void space_manager_set_right_padding_for_all_spaces(struct space_manager *sm, int right_padding)
{
    sm->right_padding = right_padding;
    table_for (struct view *view, sm->view, {
        if (!view_check_flag(view, VIEW_RIGHT_PADDING)) {
            view->right_padding = right_padding;
            view_update(view);
            view_flush(view);
        }
    })
}

void space_manager_set_split_type_for_all_spaces(struct space_manager *sm, enum window_node_split split_type)
{
    sm->split_type = split_type;
    table_for (struct view *view, sm->view, {
        if (!view_check_flag(view, VIEW_SPLIT_TYPE)) {
            view->split_type = split_type;
        }
    })
}

void space_manager_set_auto_balance_for_all_spaces(struct space_manager *sm, uint32_t auto_balance)
{
    sm->auto_balance = auto_balance;
    table_for (struct view *view, sm->view, {
        if (!view_check_flag(view, VIEW_AUTO_BALANCE)) {
            view->auto_balance = auto_balance;
        }
    })
}

bool space_manager_set_padding_for_space(struct space_manager *sm, uint64_t sid, int type, int top, int bottom, int left, int right)
{
    struct view *view = space_manager_find_view(sm, sid);
    if (view->layout == VIEW_FLOAT) return false;

    if (type == TYPE_ABS) {
        view->top_padding    = top;
        view->bottom_padding = bottom;
        view->left_padding   = left;
        view->right_padding  = right;
    } else if (type == TYPE_REL) {
        view->top_padding    = add_and_clamp_to_zero(view->top_padding, top);
        view->bottom_padding = add_and_clamp_to_zero(view->bottom_padding, bottom);
        view->left_padding   = add_and_clamp_to_zero(view->left_padding, left);
        view->right_padding  = add_and_clamp_to_zero(view->right_padding, right);
    }

    view_update(view);
    view_flush(view);

    return true;
}

bool space_manager_toggle_padding_for_space(struct space_manager *sm, uint64_t sid)
{
    struct view *view = space_manager_find_view(sm, sid);
    if (view->layout == VIEW_FLOAT) return false;

    if (view_check_flag(view, VIEW_ENABLE_PADDING)) {
        view_clear_flag(view, VIEW_ENABLE_PADDING);
    } else {
        view_set_flag(view, VIEW_ENABLE_PADDING);
    }

    view_update(view);
    view_flush(view);

    return true;
}

bool space_manager_rotate_space(struct space_manager *sm, uint64_t sid, int degrees)
{
    struct view *view = space_manager_find_view(sm, sid);
    if (view->layout != VIEW_BSP) return false;

    window_node_rotate(view->root, degrees);
    view_update(view);
    view_flush(view);

    return true;
}

bool space_manager_mirror_space(struct space_manager *sm, uint64_t sid, enum window_node_split axis)
{
    struct view *view = space_manager_find_view(sm, sid);
    if (view->layout != VIEW_BSP) return false;

    window_node_mirror(view->root, axis);
    view_update(view);
    view_flush(view);

    return true;
}

bool space_manager_equalize_space(struct space_manager *sm, uint64_t sid, uint32_t axis_flag)
{
    struct view *view = space_manager_find_view(sm, sid);
    if (view->layout != VIEW_BSP) return false;

    window_node_equalize(view->root, axis_flag);
    view_update(view);
    view_flush(view);

    return true;
}

bool space_manager_balance_space(struct space_manager *sm, uint64_t sid, uint32_t axis_flag)
{
    struct view *view = space_manager_find_view(sm, sid);
    if (view->layout != VIEW_BSP) return false;

    window_node_balance(view->root, axis_flag);
    view_update(view);
    view_flush(view);

    return true;
}

struct view *space_manager_tile_window_on_space_with_insertion_point(struct space_manager *sm, struct window *window, uint64_t sid, uint32_t insertion_point)
{
    struct view *view = space_manager_find_view(sm, sid);
    if (view->layout == VIEW_FLOAT) return view;

    window_manager_adjust_layer(window, LAYER_BELOW);
    struct window_node *node = view_add_window_node_with_insertion_point(view, window, insertion_point);
    assert(node);

    if (space_is_visible(view->sid)) {
        window_node_flush(node);
    } else {
        view_set_flag(view, VIEW_IS_DIRTY);
    }

    return view;
}

struct view *space_manager_tile_window_on_space(struct space_manager *sm, struct window *window, uint64_t sid)
{
    return space_manager_tile_window_on_space_with_insertion_point(sm, window, sid, 0);
}

void space_manager_toggle_window_split(struct space_manager *sm, struct window *window)
{
    struct view *view = space_manager_find_view(sm, window_space(window->id));
    if (view->layout != VIEW_BSP) return;

    struct window_node *node = view_find_window_node(view, window->id);
    if (node && window_node_is_intermediate(node)) {
        node->parent->split = node->parent->split == SPLIT_Y ? SPLIT_X : SPLIT_Y;

        if (view->auto_balance != SPLIT_NONE) {
            window_node_balance(view->root, view->auto_balance);
            view_update(view);
            view_flush(view);
        } else {
            window_node_update(view, node->parent);
            if (space_is_visible(view->sid)) {
                window_node_flush(node->parent);
            } else {
                view_set_flag(view, VIEW_IS_DIRTY);
            }
        }
    }
}
