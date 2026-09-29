// Process assignment, invalidation, display changes and setup.
// Runs during startup and on the event-loop thread.

void space_manager_assign_process_to_space(pid_t pid, uint64_t sid)
{
    SLSProcessAssignToSpace(g_connection, pid, sid);
}

void space_manager_assign_process_to_all_spaces(pid_t pid)
{
    SLSProcessAssignToAllSpaces(g_connection, pid);
}

bool space_manager_is_window_on_active_space(struct window *window)
{
    uint64_t sid = space_manager_active_space();
    bool result = space_manager_is_window_on_space(sid, window);
    return result;
}

bool space_manager_is_window_on_space(uint64_t sid, struct window *window)
{
    int space_count;
    uint64_t *space_list = window_space_list(window->id, &space_count);
    if (!space_list) return false;

    for (int i = 0; i < space_count; ++i) {
        if (sid == space_list[i]) {
            return true;
        }
    }

    return false;
}

void space_manager_mark_spaces_invalid_for_display(struct space_manager *sm, uint32_t did)
{
    int space_count;
    uint64_t *space_list = display_space_list(did, &space_count);
    if (!space_list) return;

    uint64_t sid = display_space_id(did);
    for (int i = 0; i < space_count; ++i) {
        if (space_list[i] == sid) {
            space_manager_refresh_view(sm, sid);
        } else {
            space_manager_mark_view_invalid(sm, space_list[i]);
        }
    }
}

void space_manager_mark_spaces_invalid(struct space_manager *sm)
{
    int display_count;
    uint32_t *display_list = display_manager_active_display_list(&display_count);
    if (!display_list) return;

    for (int i = 0; i < display_count; ++i) {
        space_manager_mark_spaces_invalid_for_display(sm, display_list[i]);
    }
}

bool space_manager_refresh_application_windows(struct space_manager *sm)
{
    int refresh_count = buf_len(g_window_manager.applications_to_refresh);
    if (!refresh_count) return false;
    int window_count = g_window_manager.window.count;
    for (int i = 0; i < refresh_count; ++i) {
        struct application *application = g_window_manager.applications_to_refresh[i];
        debug("%s: %s has windows that are not yet resolved\n", __FUNCTION__, application->name);
        bool result = window_manager_add_existing_application_windows(sm, &g_window_manager, application, i);
        if (result) {
            --refresh_count;
            --i;
        }
    }
    return window_count != g_window_manager.window.count;
}

void space_manager_handle_display_add(struct space_manager *sm, uint32_t did)
{
    int space_count;
    uint64_t *space_list = display_space_list(did, &space_count);
    if (!space_list) return;

    int list_count = 0;
    struct view *view_list[sm->view.count];
    CFStringRef uuid_list[sm->view.count];

    table_for (struct view *view, sm->view, {
        view_list[list_count] = view;
        uuid_list[list_count] = view->uuid;
        ++list_count;
    })

    for (int i = 0; i < space_count; ++i) {
        uint64_t sid = space_list[i];
        CFStringRef uuid = SLSSpaceCopyName(g_connection, sid);
        if (!uuid) continue;

        for (int j = 0; j < list_count; ++j) {
            CFStringRef view_uuid = uuid_list[j];
            if (!view_uuid) continue;

            if (CFEqual(view_uuid, uuid)) {
                struct view *view = view_list[j];

                uuid_list[j] = NULL;
                view_list[j] = NULL;

                table_remove(&sm->view, &view->sid);
                CFRelease(view->uuid);

                struct space_label *label = space_manager_get_label_for_space(sm, view->sid);
                if (label) label->sid = sid;

                view->sid = sid;
                view->uuid = CFRetain(uuid);

                table_add(&sm->view, &sid, view);
                break;
            }
        }

        CFRelease(uuid);
    }

    sm->current_space_id = space_manager_active_space();
    sm->last_space_id = sm->current_space_id;
}

void space_manager_begin(struct space_manager *sm)
{
    sm->layout = VIEW_FLOAT;
    sm->split_ratio = 0.5f;
    sm->auto_balance = SPLIT_NONE;
    sm->split_type = SPLIT_AUTO;
    sm->window_placement = CHILD_SECOND;
    sm->window_insertion_point = INSERT_FOCUSED;
    sm->window_zoom_persist = true;
    sm->labels = NULL;
    sm->skip_window_focus_animation = false;
    table_init(&sm->view, 23, hash_view, compare_view);

    int display_count;
    uint32_t *display_list = display_manager_active_display_list(&display_count);
    if (!display_list) return;

    for (int i = 0; i < display_count; ++i) {
        int space_count;
        uint64_t *space_list = display_space_list(display_list[i], &space_count);
        if (!space_list) continue;

        for (int j = 0; j < space_count; ++j) {
            struct view *view = view_create(space_list[j]);
            table_add(&sm->view, &space_list[j], view);
        }
    }

    sm->current_space_id = space_manager_active_space();
    sm->last_space_id = sm->current_space_id;
    sm->did_begin = true;
}
