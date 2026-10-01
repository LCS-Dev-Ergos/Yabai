// View serialization, flushing, creation and destruction.
// Runs on the event-loop thread.

bool view_is_invalid(struct view *view)
{
    return !view_check_flag(view, VIEW_IS_VALID);
}

bool view_is_dirty(struct view *view)
{
    return view_check_flag(view, VIEW_IS_DIRTY);
}

void view_flush(struct view *view)
{
    if (space_is_visible(view->sid)) {
        window_node_flush(view->root);
        view_clear_flag(view, VIEW_IS_DIRTY);
    } else {
        view_set_flag(view, VIEW_IS_DIRTY);
    }
}

void view_serialize(FILE *rsp, struct view *view, uint64_t flags)
{
    TIME_FUNCTION;

    if (flags == 0x0) flags |= ~flags;

    bool did_output = false;
    fprintf(rsp, "{\n");

    if (flags & SPACE_PROPERTY_ID) {
        fprintf(rsp, "\t\"id\":%lld", view->sid);
        did_output = true;
    }

    if (flags & SPACE_PROPERTY_UUID) {
        if (did_output) fprintf(rsp, ",\n");

        char *uuid = ts_cfstring_copy(view->uuid);
        fprintf(rsp, "\t\"uuid\":\"%s\"", uuid ? uuid : "<unknown>");
        did_output = true;
    }

    if (flags & SPACE_PROPERTY_INDEX) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"index\":%d", space_manager_mission_control_index(view->sid));
        did_output = true;
    }

    if (flags & SPACE_PROPERTY_LABEL) {
        if (did_output) fprintf(rsp, ",\n");

        struct space_label *space_label = space_manager_get_label_for_space(&g_space_manager, view->sid);
        fprintf(rsp, "\t\"label\":\"%s\"", ts_json_text(space_label ? space_label->label : NULL));
        did_output = true;
    }

    if (flags & SPACE_PROPERTY_TYPE) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"type\":\"%s\"", view_type_str[view->layout]);
        did_output = true;
    }

    if (flags & SPACE_PROPERTY_DISPLAY) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"display\":%d", display_manager_display_id_arrangement(space_display_id(view->sid)));
        did_output = true;
    }

    if (flags & SPACE_PROPERTY_WINDOWS) {
        if (did_output) fprintf(rsp, ",\n");

        int window_count = 0;
        uint32_t *window_list = space_window_list(view->sid, &window_count, true);

        fprintf(rsp, "\t\"windows\":[");
        for (int i = 0; i < window_count; ++i) {
            if (i < window_count - 1) {
                fprintf(rsp, "%d, ", window_list[i]);
            } else {
                fprintf(rsp, "%d", window_list[i]);
            }
        }
        fprintf(rsp, "]");
        did_output = true;
    }

    if (flags & SPACE_PROPERTY_FIRST_WINDOW) {
        if (did_output) fprintf(rsp, ",\n");

        struct window_node *first_leaf = window_node_find_first_leaf(view->root);
        fprintf(rsp, "\t\"first-window\":%d", first_leaf ? first_leaf->window_order[0] : 0);
        did_output = true;
    }

    if (flags & SPACE_PROPERTY_LAST_WINDOW) {
        if (did_output) fprintf(rsp, ",\n");

        struct window_node *last_leaf = window_node_find_last_leaf(view->root);
        fprintf(rsp, "\t\"last-window\":%d", last_leaf ? last_leaf->window_order[0] : 0);
        did_output = true;
    }

    if (flags & SPACE_PROPERTY_HAS_FOCUS) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"has-focus\":%s", json_bool(view->sid == g_space_manager.current_space_id));
        did_output = true;
    }

    if (flags & SPACE_PROPERTY_IS_VISIBLE) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"is-visible\":%s", json_bool(space_is_visible(view->sid)));
        did_output = true;
    }

    if (flags & SPACE_PROPERTY_IS_FULLSCREEN) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"is-native-fullscreen\":%s", json_bool(space_is_fullscreen(view->sid)));
    }

    fprintf(rsp, "\n}");
}

void view_update(struct view *view)
{
    uint32_t did = space_display_id(view->sid);
    CGRect frame = display_bounds_constrained(did, false);
    view->root->area = area_from_cgrect(frame);

    if (view_check_flag(view, VIEW_ENABLE_PADDING)) {
        view->root->area.x += view->left_padding;
        view->root->area.w -= (view->left_padding + view->right_padding);
        view->root->area.y += view->top_padding;
        view->root->area.h -= (view->top_padding + view->bottom_padding);
    }

    window_node_update(view, view->root);
    view_set_flag(view, VIEW_IS_VALID);
    view_set_flag(view, VIEW_IS_DIRTY);
}

struct view *view_create(uint64_t sid)
{
    struct view *view = malloc(sizeof(struct view));
    memset(view, 0, sizeof(struct view));

    view->root = malloc(sizeof(struct window_node));
    memset(view->root, 0, sizeof(struct window_node));

    view->sid = sid;
    view->uuid = SLSSpaceCopyName(g_connection, sid);

    view_set_flag(view, VIEW_ENABLE_PADDING);
    view_set_flag(view, VIEW_ENABLE_GAP);

    if (space_is_user(view->sid)) {
        if (!view_check_flag(view, VIEW_LAYOUT))         view->layout         = g_space_manager.layout;
        if (!view_check_flag(view, VIEW_TOP_PADDING))    view->top_padding    = g_space_manager.top_padding;
        if (!view_check_flag(view, VIEW_BOTTOM_PADDING)) view->bottom_padding = g_space_manager.bottom_padding;
        if (!view_check_flag(view, VIEW_LEFT_PADDING))   view->left_padding   = g_space_manager.left_padding;
        if (!view_check_flag(view, VIEW_RIGHT_PADDING))  view->right_padding  = g_space_manager.right_padding;
        if (!view_check_flag(view, VIEW_WINDOW_GAP))     view->window_gap     = g_space_manager.window_gap;
        if (!view_check_flag(view, VIEW_AUTO_BALANCE))   view->auto_balance   = g_space_manager.auto_balance;
        if (!view_check_flag(view, VIEW_SPLIT_TYPE))     view->split_type     = g_space_manager.split_type;
        view_update(view);
    } else {
        view->layout = VIEW_FLOAT;
    }

    return view;
}

void view_clear(struct view *view)
{
    if (view->root) {
        if (view->root->left)  window_node_destroy(view->root->left);
        if (view->root->right) window_node_destroy(view->root->right);

        for (int i = 0; i < view->root->window_count; ++i) {
            window_manager_remove_managed_window(&g_window_manager, view->root->window_list[i]);
        }

        insert_feedback_destroy(view->root);
        memset(view->root, 0, sizeof(struct window_node));
        view_update(view);
    }
}

void view_destroy(struct view *view)
{
    if (view->root) {
        window_node_destroy(view->root);
    }

    if (view->uuid) {
        CFRelease(view->uuid);
    }
}
