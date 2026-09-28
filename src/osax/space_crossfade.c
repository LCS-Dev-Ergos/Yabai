// Cross-fades one Desktop into another, as Dock animates its own Space
// transitions.
//
// One transaction shows the destination at its ordinary level and alpha 0,
// lowers the Desktops it replaces and makes it current. Each frame raises its
// alpha; once it is opaque, the other Desktops are hidden and levels restored.
// SkyLight applies Space alpha per window, so a covered destination hides its
// wallpaper to keep it from bleeding through the destination's own windows.
//
// A navigation during a crossfade stacks its destination on top, so the blend
// on screen continues without a jump; going back to the Desktop underneath
// fades the top one out instead. A destination whose windows cover the
// display fades in without its wallpaper (see space_crossfade_wallpaper.c).
// All of this runs under window_fade_lock, and the fade worker writes the
// frames.

#define SPACE_CROSSFADE_DISPLAYS 8
#define SPACE_CROSSFADE_LAYERS   3

struct space_crossfade
{
    uint32_t display;
    CFStringRef uuid;
    // Reserved before any Desktop is made transparent. Finishing must not
    // depend on a fresh allocation after frames have already changed it.
    CFTypeRef settlement;

    // The Desktops shown on the display, bottom first; the top one animates.
    // The wallpaper window each one hides, if any.
    uint64_t spaces[SPACE_CROSSFADE_LAYERS];
    uint32_t wallpapers[SPACE_CROSSFADE_LAYERS];
    int count;

    float from;
    float to;
    float current;
    double started;
    double duration;
    double interval;
    double next_frame;
    bool display_paced;
    bool frame_ready;
};

static struct space_crossfade space_crossfades[SPACE_CROSSFADE_DISPLAYS];

#include "space_crossfade_wallpaper.c"

static struct space_crossfade *space_crossfade_find(uint32_t display)
{
    for (int i = 0; i < SPACE_CROSSFADE_DISPLAYS; ++i) {
        if (space_crossfades[i].count && space_crossfades[i].display == display) return &space_crossfades[i];
    }

    return NULL;
}

static bool space_crossfade_active(uint32_t display)
{
    return space_crossfade_find(display) != NULL;
}

// The Desktop the crossfade ends on.
static uint64_t space_crossfade_target(struct space_crossfade *fade)
{
    return fade->spaces[fade->to == 0.0f ? fade->count - 2 : fade->count - 1];
}

// SLSTransactionCommit returns no status: on macOS 27.2 its value is a
// pointer, never 0, and Dock ignores it too. We cannot tell whether
// WindowServer applied a transaction, so nothing here depends on it.
static void space_crossfade_commit(CFTypeRef transaction)
{
    SLSTransactionCommit(transaction, 0);
    CFRelease(transaction);
}

static void space_crossfade_forget(struct space_crossfade *fade)
{
    space_crossfade_show_wallpapers(fade);
    if (fade->settlement) CFRelease(fade->settlement);
    fade->settlement = NULL;
    CFRelease(fade->uuid);
    fade->count = 0;
    window_fade_display_stop(fade->display);
}

// Leaves only the target shown, every layer opaque at the ordinary level 0,
// and forgets the crossfade.
static void space_crossfade_settle(struct space_crossfade *fade, CFTypeRef transaction)
{
    uint64_t target = space_crossfade_target(fade);
    os_signpost_event_emit(window_fade_log(), OS_SIGNPOST_ID_EXCLUSIVE, "crossfade settle",
                           "display %u space %llu layers %d", fade->display, target, fade->count);

    for (int i = 0; i < fade->count; ++i) {
        if (fade->spaces[i] != target) SLSTransactionHideSpace(transaction, fade->spaces[i]);
        SLSTransactionSetSpaceAlpha(transaction, fade->spaces[i], 1.0f);
        SLSTransactionSetSpaceAbsoluteLevel(transaction, fade->spaces[i], 0);
    }

    space_crossfade_forget(fade);
}

static void space_crossfade_finish(struct space_crossfade *fade)
{
    CFTypeRef transaction = fade->settlement;
    fade->settlement = NULL;

    space_crossfade_settle(fade, transaction);
    space_crossfade_commit(transaction);
}

// Other Desktop operations first end every crossfade at its target.
static void space_crossfade_finish_all(void)
{
    pthread_mutex_lock(&window_fade_lock);

    for (int i = 0; i < SPACE_CROSSFADE_DISPLAYS; ++i) {
        if (space_crossfades[i].count) space_crossfade_finish(&space_crossfades[i]);
    }

    pthread_mutex_unlock(&window_fade_lock);
}

// Whether a running crossfade shows `sid`.
static bool space_crossfade_holds(uint64_t sid)
{
    for (int i = 0; i < SPACE_CROSSFADE_DISPLAYS; ++i) {
        for (int j = 0; j < space_crossfades[i].count; ++j) {
            if (space_crossfades[i].spaces[j] == sid) return true;
        }
    }

    return false;
}

// Puts every Desktop that is not at rest back at alpha 1 and level 0, with its
// wallpaper window opaque, when the payload loads. SkyLight keeps a Desktop's
// alpha and level until Dock sets them again, and Dock's own switches and
// Mission Control leave them as they are: a crossfade that did not settle,
// cut short by a Dock crash for instance, would leave its Desktops
// transparent for good. Only user Desktops (type 0) crossfade, and those of a
// running crossfade are left to it. Returns how many it restored.
static int space_crossfade_restore(void)
{
    int cid = SLSMainConnectionID();
    CFArrayRef displays = SLSCopyManagedDisplaySpaces(cid);
    if (!displays) return 0;

    pthread_mutex_lock(&window_fade_lock);

    CFTypeRef transaction = NULL;
    int restored = 0;

    for (CFIndex i = 0; i < CFArrayGetCount(displays); ++i) {
        CFDictionaryRef display = CFArrayGetValueAtIndex(displays, i);
        CFArrayRef spaces = CFDictionaryGetValue(display, CFSTR("Spaces"));
        if (!spaces) continue;

        for (CFIndex j = 0; j < CFArrayGetCount(spaces); ++j) {
            CFDictionaryRef space = CFArrayGetValueAtIndex(spaces, j);
            CFNumberRef sid_ref = CFDictionaryGetValue(space, CFSTR("id64"));
            CFNumberRef type_ref = CFDictionaryGetValue(space, CFSTR("type"));

            uint64_t sid = 0;
            int type = -1;
            if (!sid_ref || !CFNumberGetValue(sid_ref, kCFNumberSInt64Type, &sid) || !sid) continue;
            if (!type_ref || !CFNumberGetValue(type_ref, kCFNumberIntType, &type) || type != 0) continue;

            if (space_crossfade_holds(sid)) continue;

            bool restore = false;
            double covered;
            float alpha = 1.0f;
            uint32_t wallpaper = space_crossfade_scan(sid, CGRectNull, &covered);

            if (wallpaper && SLSGetWindowAlpha(cid, wallpaper, &alpha) == kCGErrorSuccess && alpha < 1.0f) {
                SLSSetWindowAlpha(cid, wallpaper, 1.0f);
                restore = true;
            }

            if (SLSSpaceGetAlpha(cid, sid) != 1.0f || SLSSpaceGetAbsoluteLevel(cid, sid) != 0) {
                if (!transaction) transaction = SLSTransactionCreate(cid);

                if (transaction) {
                    SLSTransactionSetSpaceAlpha(transaction, sid, 1.0f);
                    SLSTransactionSetSpaceAbsoluteLevel(transaction, sid, 0);
                    restore = true;
                }
            }

            restored += restore;
        }
    }

    if (transaction) space_crossfade_commit(transaction);

    pthread_mutex_unlock(&window_fade_lock);
    CFRelease(displays);
    return restored;
}

#include "space_crossfade_start.c"
#include "space_crossfade_frames.c"
