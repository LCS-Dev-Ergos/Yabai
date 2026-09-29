// Query serialization for WindowServer windows without AX objects.
// Runs on the event-loop thread.

static inline const char *window_layer(int level)
{
    if (level == g_layer_below_window_level)  return layer_str[LAYER_BELOW];
    if (level == g_layer_normal_window_level) return layer_str[LAYER_NORMAL];
    if (level == g_layer_above_window_level)  return layer_str[LAYER_ABOVE];
    return "unknown";
}

void window_nonax_serialize(FILE *rsp, uint32_t wid, uint64_t flags)
{
    TIME_FUNCTION;

    if (flags == 0x0) flags |= ~flags;

    int connection;
    pid_t pid;
    uint64_t sid;
    int level;
    int sub_level;

    if ((flags & WINDOW_PROPERTY_PID) ||
        (flags & WINDOW_PROPERTY_APP)) {
        SLSGetWindowOwner(g_connection, wid, &connection);
        SLSConnectionGetPID(connection, &pid);
    }

    if ((flags & WINDOW_PROPERTY_DISPLAY) ||
        (flags & WINDOW_PROPERTY_SPACE) ||
        (flags & WINDOW_PROPERTY_IS_FULLSCREEN)) {
        sid = window_space(wid);
    }

    if ((flags & WINDOW_PROPERTY_LEVEL) ||
        (flags & WINDOW_PROPERTY_LAYER)) {
        level = window_level(wid);
    }

    if ((flags & WINDOW_PROPERTY_SUB_LEVEL) ||
        (flags & WINDOW_PROPERTY_SUB_LAYER)) {
        sub_level = window_sub_level(wid);
    }

    bool did_output = false;
    fprintf(rsp, "{\n");

    if (flags & WINDOW_PROPERTY_ID) {
        fprintf(rsp, "\t\"id\":%d", wid);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_PID) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"pid\":%d", pid);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_APP) {
        if (did_output) fprintf(rsp, ",\n");

        static char process_name[PROC_PIDPATHINFO_MAXSIZE];
        proc_name(pid, process_name, sizeof(process_name));

        char *app = process_name;
        char *escaped_app = ts_string_escape(app);

        fprintf(rsp, "\t\"app\":\"%s\"", escaped_app ? escaped_app : app);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_TITLE) {
        if (did_output) fprintf(rsp, ",\n");

        char *title = window_property_title_ts(wid);
        char *escaped_title = ts_string_escape(title);

        fprintf(rsp, "\t\"title\":\"%s\"", escaped_title ? escaped_title : title);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_SCRATCHPAD) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"scratchpad\":\"%s\"", "");
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_FRAME) {
        if (did_output) fprintf(rsp, ",\n");

        CGRect frame;
        SLSGetWindowBounds(g_connection, wid, &frame);

        fprintf(rsp, "\t\"frame\":{\n\t\t\"x\":%.4f,\n\t\t\"y\":%.4f,\n\t\t\"w\":%.4f,\n\t\t\"h\":%.4f\n\t}", frame.origin.x, frame.origin.y, frame.size.width, frame.size.height);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_ROLE) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"role\":\"%s\"", "");
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_SUBROLE) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"subrole\":\"%s\"", "");
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_ROOT_WINDOW) {
        if (did_output) fprintf(rsp, ",\n");

        uint32_t parent_wid = window_parent(wid);
        fprintf(rsp, "\t\"root-window\":%s", json_bool(parent_wid == 0));
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

        float opacity = window_opacity(wid);
        fprintf(rsp, "\t\"opacity\":%.4f", opacity);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_SPLIT_TYPE) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"split-type\":\"%s\"", window_node_split_str[0]);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_SPLIT_CHILD) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"split-child\":\"%s\"", window_node_child_str[CHILD_NONE]);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_STACK_INDEX) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"stack-index\":%d", 0);
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_CAN_MOVE) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"can-move\":%s", json_bool(false));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_CAN_RESIZE) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"can-resize\":%s", json_bool(false));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_HAS_FOCUS) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"has-focus\":%s", json_bool(false));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_HAS_SHADOW) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"has-shadow\":%s", json_bool(window_shadow(wid)));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_HAS_PARENT_ZOOM) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"has-parent-zoom\":%s", json_bool(false));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_HAS_FULLSCREEN_ZOOM) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"has-fullscreen-zoom\":%s", json_bool(false));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_HAS_AX_REFERENCE) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"has-ax-reference\":%s", json_bool(false));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_FULLSCREEN) {
        if (did_output) fprintf(rsp, ",\n");

        bool is_fullscreen = space_is_fullscreen(sid);
        fprintf(rsp, "\t\"is-native-fullscreen\":%s", json_bool(is_fullscreen));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_VISIBLE) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"is-visible\":%s", json_bool(false));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_MINIMIZED) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"is-minimized\":%s", json_bool(false));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_HIDDEN) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"is-hidden\":%s", json_bool(false));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_FLOATING) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"is-floating\":%s", json_bool(false));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_STICKY) {
        if (did_output) fprintf(rsp, ",\n");

        bool is_sticky = window_is_sticky(wid);
        fprintf(rsp, "\t\"is-sticky\":%s", json_bool(is_sticky));
        did_output = true;
    }

    if (flags & WINDOW_PROPERTY_IS_GRABBED) {
        if (did_output) fprintf(rsp, ",\n");

        fprintf(rsp, "\t\"is-grabbed\":%s", json_bool(false));
    }

    fprintf(rsp, "\n}");
}
