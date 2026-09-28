// Draw before ordering the window: a remote CA layer can present uninitialized
// white backing during prepare/cancel bursts. The copy costs preparation time,
// but does not depend on an asynchronous layer upload completing before Dock.

static void space_snapshot_surface_destroy(struct space_snapshot *snapshot)
{
    if (snapshot->backing) CGContextRelease(snapshot->backing);
}

static bool space_snapshot_surface_create(struct space_snapshot *snapshot, CGImageRef image, CGRect bounds)
{
    // The capture is in the display's own colour space. A window in any other
    // space makes the draw convert every pixel, about 50 ms for a 4112 x 2658
    // capture against 10-25 ms without, and clips the colours that space
    // lacks, so the image would not match the screen it replaces. A refusal
    // only costs the conversion.
    CGColorSpaceRef colors = CGImageGetColorSpace(image);
    if (colors) SLSSetWindowColorSpace(SLSMainConnectionID(), snapshot->window, colors);

    snapshot->backing = SLWindowContextCreate(SLSMainConnectionID(), snapshot->window, NULL);
    if (!snapshot->backing) return false;

    bounds.origin = CGPointZero;
    CGContextSetInterpolationQuality(snapshot->backing, kCGInterpolationNone);
    CGContextSetBlendMode(snapshot->backing, kCGBlendModeCopy);
    CGContextDrawImage(snapshot->backing, bounds, image);
    CGContextFlush(snapshot->backing);
    return true;
}

// Sticky windows are temporarily hidden and reattached during the ordinary
// switch. That produces destination -> outgoing image -> destination. Keep
// this owned overlay outside both user Spaces, as SketchyBar does for its bar.
static bool space_snapshot_space_create(struct space_snapshot *snapshot)
{
    int cid = SLSMainConnectionID();
    snapshot->overlay_space = SLSSpaceCreate(cid, 1, 0);
    if (!snapshot->overlay_space) return false;

    space_snapshot_space_created((uint64_t) snapshot->overlay_space);

    CFNumberRef sid = CFNumberCreate(NULL, kCFNumberIntType, &snapshot->overlay_space);
    CFNumberRef wid = CFNumberCreate(NULL, kCFNumberSInt32Type, &snapshot->window);
    CFArrayRef spaces = sid ? CFArrayCreate(NULL, (const void **)&sid, 1, &kCFTypeArrayCallBacks) : NULL;
    CFArrayRef windows = wid ? CFArrayCreate(NULL, (const void **)&wid, 1, &kCFTypeArrayCallBacks) : NULL;
    bool success = false;
    if (spaces && windows) {
        SLSSpaceSetAbsoluteLevel(cid, snapshot->overlay_space, 0);
        SLSSpaceAddWindowsAndRemoveFromSpaces(cid, snapshot->overlay_space, windows, 7);
        SLSShowSpaces(cid, spaces);
        // These mutators do not provide a reliable CGError. Verify membership
        // including unordered windows, rather than interpreting their return
        // register as success/failure. CopySpacesForWindows omits this Space.
        uint64_t set_tags = 0, clear_tags = 0;
        CFArrayRef assigned = SLSCopyWindowsWithOptionsAndTags(cid, 0, spaces, 7, &set_tags, &clear_tags);
        success = assigned && CFArrayGetCount(assigned) == 1
            && CFEqual(CFArrayGetValueAtIndex(assigned, 0), wid);
        if (assigned) CFRelease(assigned);
    }

    if (windows) CFRelease(windows);
    if (spaces) CFRelease(spaces);
    if (wid) CFRelease(wid);
    if (sid) CFRelease(sid);
    return success;
}
