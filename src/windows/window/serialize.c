// Query serialization for tracked window objects.
// Runs on the event-loop thread.

void window_serialize(FILE *rsp, struct window *window, uint64_t flags)
{
    TIME_FUNCTION;

    if (flags == 0x0) flags |= ~flags;

    uint64_t sid;
    int level;
    int sub_level;
    struct view *view;
    struct window_node *node;
    bool is_minimized;
    bool is_sticky;

    if ((flags & WINDOW_PROPERTY_DISPLAY) ||
        (flags & WINDOW_PROPERTY_SPACE) ||
        (flags & WINDOW_PROPERTY_IS_VISIBLE)) {
        sid = window_space(window->id);
    }

    if ((flags & WINDOW_PROPERTY_LEVEL) ||
        (flags & WINDOW_PROPERTY_LAYER)) {
        level = window_level(window->id);
    }

    if ((flags & WINDOW_PROPERTY_SUB_LEVEL) ||
        (flags & WINDOW_PROPERTY_SUB_LAYER)) {
        sub_level = window_sub_level(window->id);
    }

    if ((flags & WINDOW_PROPERTY_SPLIT_TYPE) ||
        (flags & WINDOW_PROPERTY_SPLIT_CHILD) ||
        (flags & WINDOW_PROPERTY_STACK_INDEX) ||
        (flags & WINDOW_PROPERTY_HAS_PARENT_ZOOM) ||
        (flags & WINDOW_PROPERTY_HAS_FULLSCREEN_ZOOM)) {
        view = window_manager_find_managed_window(&g_window_manager, window);
        node = view ? view_find_window_node(view, window->id) : NULL;
    }

    if ((flags & WINDOW_PROPERTY_IS_VISIBLE) ||
        (flags & WINDOW_PROPERTY_IS_MINIMIZED)) {
        is_minimized = window_check_flag(window, WINDOW_MINIMIZE);
    }

    if ((flags & WINDOW_PROPERTY_IS_VISIBLE) ||
        (flags & WINDOW_PROPERTY_IS_STICKY)) {
        is_sticky = window_check_flag(window, WINDOW_STICKY) || window_is_sticky(window->id);
    }

    bool did_output = false;
    fprintf(rsp, "{\n");

    if (flags & WINDOW_PROPERTY_ID) {
        fprintf(rsp, "\t\"id\":%d", window->id);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_PID) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"pid\":%d", window->application->pid);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_APP) {
        if (did_output) fprintf(rsp, ",\n");

        char *app = window->application->name;
        char *escaped_app = ts_string_escape(app);

        fprintf(rsp, "\t\"app\":\"%s\"", escaped_app ? escaped_app : app);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_TITLE) {
        if (did_output) fprintf(rsp, ",\n");

        char *title = window_title_ts(window);
        char *escaped_title = ts_string_escape(title);

        fprintf(rsp, "\t\"title\":\"%s\"", escaped_title ? escaped_title : title);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_SCRATCHPAD) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"scratchpad\":\"%s\"", ts_json_text(window->scratchpad));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_FRAME) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"frame\":{\n\t\t\"x\":%.4f,\n\t\t\"y\":%.4f,\n\t\t\"w\":%.4f,\n\t\t\"h\":%.4f\n\t}", window->frame.origin.x, window->frame.origin.y, window->frame.size.width, window->frame.size.height);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_ROLE) {
        if (did_output) fprintf(rsp, ",\n");

        char *role = window_role_ts(window);
        fprintf(rsp, "\t\"role\":\"%s\"", role);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_SUBROLE) {
        if (did_output) fprintf(rsp, ",\n");

        char *subrole = window_subrole_ts(window);
        fprintf(rsp, "\t\"subrole\":\"%s\"", subrole);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_ROOT_WINDOW) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"root-window\":%s", json_bool(window->is_root));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_DISPLAY) {
        if (did_output) fprintf(rsp, ",\n");

        int display = display_manager_display_id_arrangement(space_display_id(sid));
        fprintf(rsp, "\t\"display\":%d", display);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_SPACE) {
        if (did_output) fprintf(rsp, ",\n");

        int space = space_manager_mission_control_index(sid);
        fprintf(rsp, "\t\"space\":%d", space);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_LEVEL) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"level\":%d", level);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_SUB_LEVEL) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"sub-level\":%d", sub_level);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_LAYER) {
        if (did_output) fprintf(rsp, ",\n");

        const char *layer = window_layer(level);
        fprintf(rsp, "\t\"layer\":\"%s\"", layer);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_SUB_LAYER) {
        if (did_output) fprintf(rsp, ",\n");

        const char *sub_layer = window_layer(sub_level);
        fprintf(rsp, "\t\"sub-layer\":\"%s\"", sub_layer);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_OPACITY) {
        if (did_output) fprintf(rsp, ",\n");

        float opacity = window_opacity(window->id);
        fprintf(rsp, "\t\"opacity\":%.4f", opacity);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_SPLIT_TYPE) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"split-type\":\"%s\"", window_node_split_str[node && node->parent ? node->parent->split : 0]);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_SPLIT_CHILD) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"split-child\":\"%s\"", window_node_child_str[node ? window_node_is_left_child(node) ? CHILD_FIRST : CHILD_SECOND : CHILD_NONE]);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_STACK_INDEX) {
        if (did_output) fprintf(rsp, ",\n");

        int stack_index = node && node->window_count > 1 ? window_node_index_of_window(node, window->id)+1 : 0;
        fprintf(rsp, "\t\"stack-index\":%d", stack_index);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_CAN_MOVE) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"can-move\":%s", json_bool(window_can_move(window)));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_CAN_RESIZE) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"can-resize\":%s", json_bool(window_can_resize(window)));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_HAS_FOCUS) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"has-focus\":%s", json_bool(window->id == g_window_manager.focused_window_id));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_HAS_SHADOW) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"has-shadow\":%s", json_bool(window_shadow(window->id)));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_HAS_PARENT_ZOOM) {
        if (did_output) fprintf(rsp, ",\n");

        bool zoom_parent = node && node->zoom && node->zoom == node->parent;
        fprintf(rsp, "\t\"has-parent-zoom\":%s", json_bool(zoom_parent));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_HAS_FULLSCREEN_ZOOM) {
        if (did_output) fprintf(rsp, ",\n");

        bool zoom_fullscreen = node && node->zoom && node->zoom == view->root;
        fprintf(rsp, "\t\"has-fullscreen-zoom\":%s", json_bool(zoom_fullscreen));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_HAS_AX_REFERENCE) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"has-ax-reference\":%s", json_bool(true));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_FULLSCREEN) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"is-native-fullscreen\":%s", json_bool(window_check_flag(window, WINDOW_FULLSCREEN)));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_VISIBLE) {
        if (did_output) fprintf(rsp, ",\n");

        uint8_t ordered_in = 0;
        SLSWindowIsOrderedIn(g_connection, window->id, &ordered_in);

        bool visible = ordered_in && !is_minimized && !window->application->is_hidden && (is_sticky || space_is_visible(sid));
        fprintf(rsp, "\t\"is-visible\":%s", json_bool(visible));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_MINIMIZED) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"is-minimized\":%s", json_bool(is_minimized));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_HIDDEN) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"is-hidden\":%s", json_bool(window->application->is_hidden));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_FLOATING) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"is-floating\":%s", json_bool(window_check_flag(window, WINDOW_FLOAT)));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_STICKY) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"is-sticky\":%s", json_bool(is_sticky));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_GRABBED) {
        if (did_output) fprintf(rsp, ",\n");

        bool grabbed = window == g_mouse_state.window;
        fprintf(rsp, "\t\"is-grabbed\":%s", json_bool(grabbed));
    }

    fprintf(rsp, "\n}");
}
