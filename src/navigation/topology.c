// Answers a navigation's questions about Desktops from one WindowServer reply.
//
// SLSCopyManagedDisplayForSpace, behind space_display_id and space_is_visible,
// asks WindowServer for the state of every Desktop on each call. Profiling
// isolated switches on lcs.13 put those lookups and the per-window Space
// queries of the cross-display raise check at over half of the request.
// SLSCopyManagedDisplaySpaces returns the same state once: mission-control
// order, the display and type of every Desktop and each display's current one.
//
// The snapshot covers one request and is taken before the switch; afterwards
// only the current Desktops of the other displays are read. A Desktop it does
// not know, or a reply it cannot read, falls back to asking WindowServer.

#define SPACE_NAVIGATION_SPACES_MAX 128

static struct
{
    bool loaded;

    int count;
    uint64_t sid[SPACE_NAVIGATION_SPACES_MAX];
    uint32_t display[SPACE_NAVIGATION_SPACES_MAX];
    int type[SPACE_NAVIGATION_SPACES_MAX];

    int display_count;
    uint32_t display_id[SPACE_NAVIGATION_DISPLAYS_MAX];
    uint64_t current[SPACE_NAVIGATION_DISPLAYS_MAX];
} g_space_navigation_spaces;

static bool space_navigation_spaces_number(CFTypeRef dictionary, CFStringRef key, uint64_t *value)
{
    if (!dictionary || CFGetTypeID(dictionary) != CFDictionaryGetTypeID()) return false;

    CFTypeRef number = CFDictionaryGetValue(dictionary, key);
    if (!number || CFGetTypeID(number) != CFNumberGetTypeID()) return false;

    return CFNumberGetValue(number, kCFNumberSInt64Type, value);
}

// Reads a SLSCopyManagedDisplaySpaces reply. Anything unexpected leaves the
// snapshot unloaded, so that every lookup asks WindowServer instead.
static void space_navigation_spaces_load(CFArrayRef displays)
{
    g_space_navigation_spaces.loaded = false;
    g_space_navigation_spaces.count = 0;
    g_space_navigation_spaces.display_count = 0;

    if (!displays || CFGetTypeID(displays) != CFArrayGetTypeID()) return;
    if (CFArrayGetCount(displays) > SPACE_NAVIGATION_DISPLAYS_MAX) return;

    for (CFIndex i = 0; i < CFArrayGetCount(displays); ++i) {
        CFDictionaryRef display = CFArrayGetValueAtIndex(displays, i);
        if (!display || CFGetTypeID(display) != CFDictionaryGetTypeID()) return;

        CFStringRef uuid = CFDictionaryGetValue(display, CFSTR("Display Identifier"));
        CFArrayRef spaces = CFDictionaryGetValue(display, CFSTR("Spaces"));
        if (!uuid || CFGetTypeID(uuid) != CFStringGetTypeID()) return;
        if (!spaces || CFGetTypeID(spaces) != CFArrayGetTypeID()) return;

        uint32_t did = display_id(uuid);
        uint64_t current = 0;
        space_navigation_spaces_number(CFDictionaryGetValue(display, CFSTR("Current Space")), CFSTR("id64"), &current);

        g_space_navigation_spaces.display_id[i] = did;
        g_space_navigation_spaces.current[i] = current;
        g_space_navigation_spaces.display_count = (int) i + 1;

        for (CFIndex j = 0; j < CFArrayGetCount(spaces); ++j) {
            int index = g_space_navigation_spaces.count;
            if (index == SPACE_NAVIGATION_SPACES_MAX) return;

            uint64_t sid = 0;
            uint64_t type = 0;
            CFDictionaryRef space = CFArrayGetValueAtIndex(spaces, j);
            if (!space_navigation_spaces_number(space, CFSTR("id64"), &sid) || !sid) return;

            g_space_navigation_spaces.sid[index] = sid;
            g_space_navigation_spaces.display[index] = did;
            g_space_navigation_spaces.type[index] = space_navigation_spaces_number(space, CFSTR("type"), &type) ? (int) type : -1;
            g_space_navigation_spaces.count = index + 1;
        }
    }

    g_space_navigation_spaces.loaded = true;
}

// Takes the snapshot for one request or queued step.
static void space_navigation_spaces_read(void)
{
    CFArrayRef displays = SLSCopyManagedDisplaySpaces(g_connection);
    space_navigation_spaces_load(displays);
    if (displays) CFRelease(displays);
}

static bool space_navigation_spaces_loaded(void)
{
    return g_space_navigation_spaces.loaded;
}

// Ends the request's snapshot; later lookups ask WindowServer.
static void space_navigation_spaces_unload(void)
{
    g_space_navigation_spaces.loaded = false;
}

// The 1-based mission-control index of sid, or 0.
static int space_navigation_spaces_index(uint64_t sid)
{
    for (int i = 0; g_space_navigation_spaces.loaded && i < g_space_navigation_spaces.count; ++i) {
        if (g_space_navigation_spaces.sid[i] == sid) return i + 1;
    }

    return 0;
}

static uint64_t space_navigation_spaces_at(int index)
{
    if (!g_space_navigation_spaces.loaded) return space_manager_mission_control_space(index);
    if (index < 1 || index > g_space_navigation_spaces.count) return 0;

    return g_space_navigation_spaces.sid[index - 1];
}

// The space `steps` places after the space at `index` (1-based) of `count`,
// wrapping around at both ends.
static int space_navigation_step_index(int index, int count, int steps)
{
    int result = (index - 1 + steps) % count;
    if (result < 0) result += count;

    return result + 1;
}

// The Desktop `steps` places after sid in mission-control order, wrapping,
// or 0 when the snapshot does not know sid.
static uint64_t space_navigation_spaces_offset(uint64_t sid, int steps)
{
    int index = space_navigation_spaces_index(sid);
    if (!index) return 0;

    int count = g_space_navigation_spaces.count;
    return space_navigation_spaces_at(space_navigation_step_index(index, count, steps));
}

static uint32_t space_navigation_space_display(uint64_t sid)
{
    int index = space_navigation_spaces_index(sid);
    if (!index || !g_space_navigation_spaces.display[index - 1]) return space_display_id(sid);

    return g_space_navigation_spaces.display[index - 1];
}

static uint64_t space_navigation_display_space(uint32_t did)
{
    for (int i = 0; g_space_navigation_spaces.loaded && i < g_space_navigation_spaces.display_count; ++i) {
        if (g_space_navigation_spaces.display_id[i] == did && g_space_navigation_spaces.current[i]) {
            return g_space_navigation_spaces.current[i];
        }
    }

    return display_space_id(did);
}

static bool space_navigation_space_visible(uint64_t sid)
{
    uint32_t did = space_navigation_space_display(sid);
    return did && space_navigation_display_space(did) == sid;
}

static bool space_navigation_space_fullscreen(uint64_t sid)
{
    int index = space_navigation_spaces_index(sid);
    if (!index || g_space_navigation_spaces.type[index - 1] < 0) return space_is_fullscreen(sid);

    return g_space_navigation_spaces.type[index - 1] == 4;
}

// What the other displays show, for the cross-display raise check.

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
