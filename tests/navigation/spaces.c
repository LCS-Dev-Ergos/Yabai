#include <CoreFoundation/CoreFoundation.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// WindowServer queries the snapshot replaces; each counts a fallback.
static int fallbacks;

static uint32_t display_id(CFStringRef uuid)
{
    if (CFEqual(uuid, CFSTR("A"))) return 1;
    if (CFEqual(uuid, CFSTR("B"))) return 2;

    return 0;
}

static uint32_t space_display_id(uint64_t sid)
{
    ++fallbacks;
    return sid == 99 ? 2 : 0;
}

static uint64_t display_space_id(uint32_t did)
{
    ++fallbacks;
    return did == 1 ? 5 : did == 2 ? 6 : 0;
}

static bool space_is_fullscreen(uint64_t sid)
{
    ++fallbacks;
    return sid == 99;
}

static uint64_t space_manager_mission_control_space(int index)
{
    ++fallbacks;
    return index == 1 ? 5 : 0;
}

static uint32_t *display_manager_active_display_list(int *count)
{
    static uint32_t list[] = { 1, 2 };

    ++fallbacks;
    *count = 2;

    return list;
}

#include "../../src/space_navigation_spaces.c"

static CFDictionaryRef space(uint64_t sid, int type)
{
    CFNumberRef id = CFNumberCreate(NULL, kCFNumberSInt64Type, &sid);
    CFNumberRef kind = CFNumberCreate(NULL, kCFNumberIntType, &type);
    const void *keys[] = { CFSTR("id64"), CFSTR("type") };
    const void *values[] = { id, kind };

    CFDictionaryRef result = CFDictionaryCreate(NULL, keys, values, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFRelease(id);
    CFRelease(kind);

    return result;
}

static CFDictionaryRef display(CFStringRef uuid, uint64_t current, CFDictionaryRef *spaces, int count)
{
    CFArrayRef list = CFArrayCreate(NULL, (const void **) spaces, count, &kCFTypeArrayCallBacks);
    CFDictionaryRef current_space = space(current, 0);
    const void *keys[] = { CFSTR("Display Identifier"), CFSTR("Current Space"), CFSTR("Spaces") };
    const void *values[] = { uuid, current_space, list };

    CFDictionaryRef result = CFDictionaryCreate(NULL, keys, values, 3, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFRelease(list);
    CFRelease(current_space);

    return result;
}

int main(void)
{
    // Display A holds Desktops 11, 12 (fullscreen) and 13; display B holds 21.
    CFDictionaryRef a_spaces[] = { space(11, 0), space(12, 4), space(13, 0) };
    CFDictionaryRef b_spaces[] = { space(21, 0) };
    CFDictionaryRef displays[] = { display(CFSTR("A"), 13, a_spaces, 3), display(CFSTR("B"), 21, b_spaces, 1) };
    CFArrayRef reply = CFArrayCreate(NULL, (const void **) displays, 2, &kCFTypeArrayCallBacks);

    space_navigation_spaces_load(reply);
    assert(g_space_navigation_spaces.loaded);

    // Mission-control order runs across displays.
    assert(space_navigation_spaces_index(11) == 1 && space_navigation_spaces_index(21) == 4);
    assert(space_navigation_spaces_at(3) == 13 && space_navigation_spaces_at(4) == 21);
    assert(space_navigation_spaces_at(0) == 0 && space_navigation_spaces_at(5) == 0);

    assert(space_navigation_space_display(12) == 1 && space_navigation_space_display(21) == 2);
    assert(space_navigation_display_space(1) == 13 && space_navigation_display_space(2) == 21);
    assert(space_navigation_space_visible(13) && space_navigation_space_visible(21));
    assert(!space_navigation_space_visible(11));
    assert(space_navigation_space_fullscreen(12) && !space_navigation_space_fullscreen(13));

    uint64_t list[SPACE_NAVIGATION_DISPLAYS_MAX];
    assert(space_navigation_spaces_visible_elsewhere(1, list) == 1 && list[0] == 21);
    assert(space_navigation_spaces_visible_elsewhere(2, list) == 1 && list[0] == 13);
    assert(fallbacks == 0);

    // A Desktop created after the snapshot is asked about as before.
    assert(space_navigation_space_display(99) == 2 && space_navigation_space_fullscreen(99));
    assert(fallbacks == 2);

    // An unreadable reply leaves every lookup to WindowServer.
    CFRelease(reply);
    const void *broken_values[] = { CFSTR("A") };
    const void *broken_keys[] = { CFSTR("Display Identifier") };
    CFDictionaryRef broken = CFDictionaryCreate(NULL, broken_keys, broken_values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    reply = CFArrayCreate(NULL, (const void **) &broken, 1, &kCFTypeArrayCallBacks);

    fallbacks = 0;
    space_navigation_spaces_load(reply);
    assert(!g_space_navigation_spaces.loaded);
    assert(space_navigation_spaces_index(11) == 0 && space_navigation_spaces_at(1) == 5);
    assert(space_navigation_display_space(1) == 5);
    assert(space_navigation_spaces_visible_elsewhere(1, list) == 1 && list[0] == 6);
    assert(fallbacks == 4);

    space_navigation_spaces_load(NULL);
    assert(!g_space_navigation_spaces.loaded);

    CFRelease(reply);
    CFRelease(broken);
    for (int i = 0; i < 3; ++i) CFRelease(a_spaces[i]);
    CFRelease(b_spaces[0]);
    CFRelease(displays[0]);
    CFRelease(displays[1]);

    puts("navigation spaces: snapshot order, displays, visibility, type and fallback checks passed");

    return 0;
}
