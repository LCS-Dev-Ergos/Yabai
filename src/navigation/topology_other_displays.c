// Fork: what the other displays show, for the cross-display raise check.

#define SPACE_NAVIGATION_WINDOWS_MAX 256

// The current Desktops of the displays other than `did`, at most
// SPACE_NAVIGATION_DISPLAYS_MAX of them.
static int space_navigation_spaces_visible_elsewhere(uint32_t did, uint64_t *list)
{
    int count = 0;

    if (g_space_navigation_spaces.loaded) {
        for (int i = 0; i < g_space_navigation_spaces.display_count; ++i) {
            uint32_t other = g_space_navigation_spaces.display_id[i];
            uint64_t sid = other && other != did ? space_navigation_display_space(other) : 0;
            if (sid) list[count++] = sid;
        }

        return count;
    }

    int display_count = 0;
    uint32_t *displays = display_manager_active_display_list(&display_count);

    for (int i = 0; displays && i < display_count && count < SPACE_NAVIGATION_DISPLAYS_MAX; ++i) {
        uint64_t sid = displays[i] != did ? display_space_id(displays[i]) : 0;
        if (sid) list[count++] = sid;
    }

    return count;
}

// The ids of the windows `cid` owns on the given Desktops, at most `capacity`.
// The caller checks them against managed windows, so this skips the query
// space_window_list_for_connection makes to classify each window.
static int space_navigation_spaces_windows(uint64_t *spaces, int space_count, int cid, uint32_t *ids, int capacity)
{
    if (space_count < 1 || space_count > SPACE_NAVIGATION_DISPLAYS_MAX) return 0;

    CFNumberRef numbers[SPACE_NAVIGATION_DISPLAYS_MAX];
    for (int i = 0; i < space_count; ++i) {
        numbers[i] = CFNumberCreate(NULL, kCFNumberSInt64Type, &spaces[i]);
    }

    CFArrayRef space_list = CFArrayCreate(NULL, (const void **) numbers, space_count, &kCFTypeArrayCallBacks);
    for (int i = 0; i < space_count; ++i) {
        CFRelease(numbers[i]);
    }

    uint64_t set_tags = 0;
    uint64_t clear_tags = 0;

    CFArrayRef window_list = SLSCopyWindowsWithOptionsAndTags(g_connection, cid, space_list, 0x2, &set_tags, &clear_tags);
    CFRelease(space_list);

    if (!window_list) return 0;

    int count = 0;
    for (CFIndex i = 0; i < CFArrayGetCount(window_list) && count < capacity; ++i) {
        CFNumberRef number = CFArrayGetValueAtIndex(window_list, i);
        if (CFGetTypeID(number) != CFNumberGetTypeID()) continue;

        if (CFNumberGetValue(number, kCFNumberSInt32Type, &ids[count])) ++count;
    }

    CFRelease(window_list);

    return count;
}
