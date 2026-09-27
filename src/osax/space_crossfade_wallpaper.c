// Fork: the destination's wallpaper during a crossfade; see space_crossfade.c.
//
// WindowServer applies a Desktop's alpha to each of its windows, not to the
// Desktop as a whole. While the destination fades in, its windows are
// translucent over its own wallpaper, and the wallpaper shows through them as
// it did through the window fade. With the destination's wallpaper window
// transparent for the crossfade, its windows blend straight over the Desktop
// below, and every channel moves monotonically from one Desktop to the other.
//
// Where the destination's windows leave the display uncovered, the Desktop
// below shows until the crossfade ends and the wallpaper appears. So only a
// Desktop whose ordinary windows cover most of the display hides its
// wallpaper; the gaps between tiled windows then show the wallpaper of the
// Desktop below, the same picture when Desktops share one.

#define SPACE_CROSSFADE_COVER 0.8

#include "space_crossfade_coverage.c"

// A low window level alone does not identify a wallpaper. Check the owner
// executable before changing another process's alpha; an unreadable owner
// leaves the window untouched.
static bool space_crossfade_is_wallpaper(uint32_t wid)
{
    int owner = 0;
    pid_t pid = 0;
    char path[PROC_PIDPATHINFO_MAXSIZE];

    if (SLSGetWindowOwner(SLSMainConnectionID(), wid, &owner) != kCGErrorSuccess) return false;
    if (SLSConnectionGetPID(owner, &pid) != kCGErrorSuccess || pid <= 0) return false;
    if (proc_pidpath(pid, path, sizeof(path)) <= 0) return false;

    return strcmp(path, "/System/Library/CoreServices/WindowManager.app/Contents/MacOS/WindowManager") == 0;
}

// The wallpaper window of `sid`, WindowManager's at or below the desktop
// window level, or 0. *covered receives the area of `bounds` that the
// Desktop's ordinary windows (normal to modal panel level) cover, counting
// each pixel only once. If too many rectangles are present, the bounded
// subset underestimates coverage instead of hiding a wallpaper by mistake.
static uint32_t space_crossfade_scan(uint64_t sid, CGRect bounds, double *covered)
{
    int cid = SLSMainConnectionID();
    uint64_t set_tags = 0;
    uint64_t clear_tags = 0;

    CFNumberRef sid_ref = CFNumberCreate(NULL, kCFNumberSInt64Type, &sid);
    *covered = 0.0;
    if (!sid_ref) return 0;

    CFArrayRef spaces = CFArrayCreate(NULL, (const void **) &sid_ref, 1, &kCFTypeArrayCallBacks);
    CFArrayRef windows = spaces ? SLSCopyWindowsWithOptionsAndTags(cid, 0, spaces, 0x2, &set_tags, &clear_tags) : NULL;

    if (spaces) CFRelease(spaces);
    CFRelease(sid_ref);

    if (!windows) return 0;

    int desktop = CGWindowLevelForKey(kCGDesktopWindowLevelKey);
    uint32_t wallpaper = 0;
    CGRect rects[SPACE_CROSSFADE_RECTS];
    int rect_count = 0;

    for (CFIndex i = 0; i < CFArrayGetCount(windows); ++i) {
        uint32_t wid = 0;
        int level = 0;

        CFNumberRef wid_ref = CFArrayGetValueAtIndex(windows, i);
        if (!CFNumberGetValue(wid_ref, kCFNumberSInt32Type, &wid) || !wid) continue;
        if (SLSGetWindowLevel(cid, wid, &level) != kCGErrorSuccess) continue;

        if (level <= desktop) {
            if (!wallpaper && space_crossfade_is_wallpaper(wid)) wallpaper = wid;
            continue;
        }

        if (CGRectIsNull(bounds) || rect_count == SPACE_CROSSFADE_RECTS) continue;
        if (level < kCGNormalWindowLevel || level > kCGModalPanelWindowLevel) continue;

        CGRect frame;
        if (SLSGetWindowBounds(cid, wid, &frame) != kCGErrorSuccess) continue;

        CGRect visible = CGRectIntersection(frame, bounds);
        if (CGRectIsEmpty(visible) || CGRectIsNull(visible)) continue;
        if (!isfinite(visible.origin.x) || !isfinite(visible.origin.y)
            || !isfinite(CGRectGetMaxX(visible)) || !isfinite(CGRectGetMaxY(visible))) continue;

        rects[rect_count++] = visible;
    }

    *covered = space_crossfade_covered_area(rects, rect_count);
    CFRelease(windows);
    return wallpaper;
}

// The wallpaper window to hide while `sid` fades in on `display`, or 0.
static uint32_t space_crossfade_wallpaper(uint32_t display, uint64_t sid)
{
    CGRect bounds = CGDisplayBounds(display);
    double area = bounds.size.width * bounds.size.height;
    if (!(area > 0.0)) return 0;

    double covered;
    uint32_t wallpaper = space_crossfade_scan(sid, bounds, &covered);
    return covered >= SPACE_CROSSFADE_COVER * area ? wallpaper : 0;
}

// Shows again every wallpaper the crossfade hid. Called before the settling
// transaction is committed, while the Desktops below are still shown.
static void space_crossfade_show_wallpapers(struct space_crossfade *fade)
{
    for (int i = 0; i < fade->count; ++i) {
        if (!fade->wallpapers[i]) continue;

        SLSSetWindowAlpha(SLSMainConnectionID(), fade->wallpapers[i], 1.0f);
        fade->wallpapers[i] = 0;
    }
}
