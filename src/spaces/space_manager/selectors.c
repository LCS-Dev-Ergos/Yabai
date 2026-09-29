// Desktop indexes and relative Space selection.
// Runs on the event-loop thread.

int space_manager_mission_control_index(uint64_t sid)
{
    uint64_t result = 0;
    int desktop_cnt = 1;

    CFArrayRef display_spaces_ref = SLSCopyManagedDisplaySpaces(g_connection);
    if (!display_spaces_ref) return 0;

    int display_spaces_count = CFArrayGetCount(display_spaces_ref);
    for (int i = 0; i < display_spaces_count; ++i) {
        CFDictionaryRef display_ref = CFArrayGetValueAtIndex(display_spaces_ref, i);
        CFArrayRef spaces_ref = CFDictionaryGetValue(display_ref, CFSTR("Spaces"));
        int spaces_count = CFArrayGetCount(spaces_ref);

        for (int j = 0; j < spaces_count; ++j) {
            CFDictionaryRef space_ref = CFArrayGetValueAtIndex(spaces_ref, j);
            CFNumberRef sid_ref = CFDictionaryGetValue(space_ref, CFSTR("id64"));
            CFNumberGetValue(sid_ref, CFNumberGetType(sid_ref), &result);
            if (sid == result) goto out;

            ++desktop_cnt;
        }
    }

    desktop_cnt = 0;
out:
    CFRelease(display_spaces_ref);
    return desktop_cnt;
}

uint64_t space_manager_mission_control_space(int desktop_id)
{
    uint64_t result = 0;
    int desktop_cnt = 1;

    CFArrayRef display_spaces_ref = SLSCopyManagedDisplaySpaces(g_connection);
    if (!display_spaces_ref) return 0;

    int display_spaces_count = CFArrayGetCount(display_spaces_ref);
    for (int i = 0; i < display_spaces_count; ++i) {
        CFDictionaryRef display_ref = CFArrayGetValueAtIndex(display_spaces_ref, i);
        CFArrayRef spaces_ref = CFDictionaryGetValue(display_ref, CFSTR("Spaces"));
        int spaces_count = CFArrayGetCount(spaces_ref);

        for (int j = 0; j < spaces_count; ++j) {
            CFDictionaryRef space_ref = CFArrayGetValueAtIndex(spaces_ref, j);
            CFNumberRef sid_ref = CFDictionaryGetValue(space_ref, CFSTR("id64"));
            CFNumberGetValue(sid_ref, CFNumberGetType(sid_ref), &result);
            if (desktop_cnt == desktop_id) goto out;

            ++desktop_cnt;
        }
    }

    result = 0;
out:
    CFRelease(display_spaces_ref);
    return result;
}

uint64_t space_manager_cursor_space(void)
{
    uint32_t did = display_manager_cursor_display_id();
    return display_space_id(did);
}

uint64_t space_manager_prev_space(uint64_t sid)
{
    uint64_t p_sid = 0;
    uint64_t n_sid = 0;

    CFArrayRef display_spaces_ref = SLSCopyManagedDisplaySpaces(g_connection);
    if (!display_spaces_ref) return 0;

    int display_spaces_count = CFArrayGetCount(display_spaces_ref);
    for (int i = 0; i < display_spaces_count; ++i) {
        CFDictionaryRef display_ref = CFArrayGetValueAtIndex(display_spaces_ref, i);
        CFArrayRef spaces_ref = CFDictionaryGetValue(display_ref, CFSTR("Spaces"));
        int spaces_count = CFArrayGetCount(spaces_ref);

        for (int j = 0; j < spaces_count; ++j) {
            CFDictionaryRef space_ref = CFArrayGetValueAtIndex(spaces_ref, j);
            CFNumberRef sid_ref = CFDictionaryGetValue(space_ref, CFSTR("id64"));
            CFNumberGetValue(sid_ref, CFNumberGetType(sid_ref), &n_sid);
            if (n_sid == sid) goto out;

            p_sid = n_sid;
        }
    }

out:
    CFRelease(display_spaces_ref);
    return p_sid != sid ? p_sid : 0;
}

uint64_t space_manager_next_space(uint64_t sid)
{
    uint64_t n_sid = 0;
    bool found_sid = false;

    CFArrayRef display_spaces_ref = SLSCopyManagedDisplaySpaces(g_connection);
    if (!display_spaces_ref) return 0;

    int display_spaces_count = CFArrayGetCount(display_spaces_ref);
    for (int i = 0; i < display_spaces_count; ++i) {
        CFDictionaryRef display_ref = CFArrayGetValueAtIndex(display_spaces_ref, i);
        CFArrayRef spaces_ref = CFDictionaryGetValue(display_ref, CFSTR("Spaces"));
        int spaces_count = CFArrayGetCount(spaces_ref);

        for (int j = 0; j < spaces_count; ++j) {
            CFDictionaryRef space_ref = CFArrayGetValueAtIndex(spaces_ref, j);
            CFNumberRef sid_ref = CFDictionaryGetValue(space_ref, CFSTR("id64"));
            CFNumberGetValue(sid_ref, CFNumberGetType(sid_ref), &n_sid);
            if (found_sid) goto out;

            found_sid = n_sid == sid;
        }
    }

out:
    CFRelease(display_spaces_ref);
    return n_sid != sid ? n_sid : 0;
}

uint64_t space_manager_first_space(void)
{
    uint64_t sid = 0;

    CFArrayRef display_spaces_ref = SLSCopyManagedDisplaySpaces(g_connection);
    if (!display_spaces_ref) return 0;

    CFDictionaryRef display_ref = CFArrayGetValueAtIndex(display_spaces_ref, 0);
    CFArrayRef spaces_ref = CFDictionaryGetValue(display_ref, CFSTR("Spaces"));

    CFDictionaryRef space_ref = CFArrayGetValueAtIndex(spaces_ref, 0);
    CFNumberRef sid_ref = CFDictionaryGetValue(space_ref, CFSTR("id64"));
    CFNumberGetValue(sid_ref, CFNumberGetType(sid_ref), &sid);

    CFRelease(display_spaces_ref);
    return sid;
}

uint64_t space_manager_last_space(void)
{
    uint64_t sid = 0;

    CFArrayRef display_spaces_ref = SLSCopyManagedDisplaySpaces(g_connection);
    if (!display_spaces_ref) return 0;

    int display_spaces_count = CFArrayGetCount(display_spaces_ref);
    CFDictionaryRef display_ref = CFArrayGetValueAtIndex(display_spaces_ref, display_spaces_count-1);
    CFArrayRef spaces_ref = CFDictionaryGetValue(display_ref, CFSTR("Spaces"));
    int spaces_count = CFArrayGetCount(spaces_ref);

    CFDictionaryRef space_ref = CFArrayGetValueAtIndex(spaces_ref, spaces_count-1);
    CFNumberRef sid_ref = CFDictionaryGetValue(space_ref, CFSTR("id64"));
    CFNumberGetValue(sid_ref, CFNumberGetType(sid_ref), &sid);

    CFRelease(display_spaces_ref);
    return sid;
}

uint64_t space_manager_active_space(void)
{
    uint32_t did = 0;
    struct window *window = window_manager_focused_window(&g_window_manager);

    if (window) did = window_display_id(window->id);
    if (!did)   did = display_manager_active_display_id();
    if (!did)   return 0;

    return display_space_id(did);
}
