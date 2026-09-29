// Mission Control entry, mode changes and exit.
// Runs on the event-loop thread.

static EVENT_HANDLER(MISSION_CONTROL_SHOW_ALL_WINDOWS)
{
    space_navigation_snapshot_cancel();
    debug("%s:\n", __FUNCTION__);
    g_mission_control_mode = MISSION_CONTROL_MODE_SHOW_ALL_WINDOWS;
    event_signal_push(SIGNAL_MISSION_CONTROL_ENTER, (void*)(uintptr_t)g_mission_control_mode);
}

static EVENT_HANDLER(MISSION_CONTROL_SHOW_FRONT_WINDOWS)
{
    space_navigation_snapshot_cancel();
    debug("%s:\n", __FUNCTION__);
    g_mission_control_mode = MISSION_CONTROL_MODE_SHOW_FRONT_WINDOWS;
    event_signal_push(SIGNAL_MISSION_CONTROL_ENTER, (void*)(uintptr_t)g_mission_control_mode);
}

static EVENT_HANDLER(MISSION_CONTROL_SHOW_DESKTOP)
{
    space_navigation_snapshot_cancel();
    debug("%s:\n", __FUNCTION__);
    g_mission_control_mode = MISSION_CONTROL_MODE_SHOW_DESKTOP;
    event_signal_push(SIGNAL_MISSION_CONTROL_ENTER, (void*)(uintptr_t)g_mission_control_mode);
}

static EVENT_HANDLER(MISSION_CONTROL_ENTER)
{
    space_navigation_snapshot_cancel();
    debug("%s:\n", __FUNCTION__);
    g_mission_control_mode = MISSION_CONTROL_MODE_SHOW;

    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 0.1f * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
        event_loop_post(&g_event_loop, MISSION_CONTROL_CHECK_FOR_EXIT, NULL, 0);
    });

    event_signal_push(SIGNAL_MISSION_CONTROL_ENTER, (void*)(uintptr_t)g_mission_control_mode);
}

static EVENT_HANDLER(MISSION_CONTROL_CHECK_FOR_EXIT)
{
    if (!mission_control_is_active()) return;

    CFArrayRef window_list = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly, 0);
    int window_count = CFArrayGetCount(window_list);
    bool found = false;

    for (int i = 0; i < window_count; ++i) {
        CFDictionaryRef dictionary = CFArrayGetValueAtIndex(window_list, i);

        CFStringRef name = CFDictionaryGetValue(dictionary, kCGWindowName);
        if (name) continue;

        CFStringRef owner = CFDictionaryGetValue(dictionary, kCGWindowOwnerName);
        if (!owner) continue;

        CFNumberRef layer_ref = CFDictionaryGetValue(dictionary, kCGWindowLayer);
        if (!layer_ref) continue;

        uint64_t layer = 0;
        CFNumberGetValue(layer_ref, CFNumberGetType(layer_ref), &layer);
        if (layer != 18) continue;

        if (CFEqual(CFSTR("Dock"), owner)) {
            found = true;
            break;
        }
    }

    if (found) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 0.1f * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            event_loop_post(&g_event_loop, MISSION_CONTROL_CHECK_FOR_EXIT, NULL, 0);
        });
    } else {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 0.0f), dispatch_get_main_queue(), ^{
            event_loop_post(&g_event_loop, MISSION_CONTROL_EXIT, NULL, 0);
        });
    }

    CFRelease(window_list);
}

static EVENT_HANDLER(MISSION_CONTROL_EXIT)
{
    debug("%s:\n", __FUNCTION__);

    if (g_window_manager.menubar_opacity != 1.0f) {
        float alpha = space_is_fullscreen(g_space_manager.current_space_id) ? 1.0f : g_window_manager.menubar_opacity;
        SLSSetMenuBarInsetAndAlpha(g_connection, 0, 1, alpha);
    }

    if (g_mission_control_mode == MISSION_CONTROL_MODE_SHOW || g_mission_control_mode == MISSION_CONTROL_MODE_SHOW_ALL_WINDOWS) {
        window_manager_correct_for_mission_control_changes(&g_space_manager, &g_window_manager);
    }

    event_signal_push(SIGNAL_MISSION_CONTROL_EXIT, (void*)(uintptr_t)g_mission_control_mode);
    g_mission_control_mode = MISSION_CONTROL_MODE_INACTIVE;
}
