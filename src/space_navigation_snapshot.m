// Crossfade one already-composited outgoing frame over an ordinary Desktop
// switch. Space alpha affects shared Finder windows and Space levels reorder
// global windows; this path changes only a window owned by yabai.
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include <os/signpost.h>

#define SPACE_SNAPSHOT_CAPTURE_NS 150000000ULL
#define SPACE_SNAPSHOT_MAX_PIXELS 24000000ULL

// How long a capture may stay without its callback before it counts as lost.
#ifndef SPACE_SNAPSHOT_STALE_NS
#define SPACE_SNAPSHOT_STALE_NS 2000000000ULL
#endif

// Signposts in subsystem com.lcs.yabai, category effects: how long each
// snapshot took to capture and to prepare, and why one was not used.
static os_log_t space_snapshot_log(void)
{
    static os_log_t log;
    static dispatch_once_t once;

    dispatch_once(&once, ^{
        log = os_log_create("com.lcs.yabai", "effects");
    });

    return log;
}

static void space_snapshot_trace(const char *result, uint64_t began, uint64_t captured)
{
    uint64_t now = read_os_timer();
    double capture = captured ? (captured - began) / 1e6 : 0.0;

    os_signpost_event_emit(space_snapshot_log(), OS_SIGNPOST_ID_EXCLUSIVE, "snapshot",
                           "%{public}s capture %.1f ms prepare %.1f ms", result, capture, (now - began) / 1e6);
}

struct space_snapshot_capture
{
    int references;
    uint64_t generation;
    dispatch_semaphore_t ready;
    CGImageRef image;
};

// A timed-out capture remains the only request in flight until its callback
// arrives, so slow WindowServer responses cannot build a backlog. A callback
// that never arrived would disable the crossfade until yabai restarts: after
// SPACE_SNAPSHOT_STALE_NS the request counts as lost and another may start.
// Its callback, if it still comes, only releases its own image.
static struct
{
    pthread_mutex_t lock;
    uint64_t generation;
    uint64_t pending;       // The capture in flight, 0 when none.
    uint64_t started;
} space_snapshot_captures = { .lock = PTHREAD_MUTEX_INITIALIZER };

static uint64_t space_snapshot_capture_begin(void)
{
    uint64_t generation = 0;
    uint64_t now = read_os_timer();

    pthread_mutex_lock(&space_snapshot_captures.lock);

    if (!space_snapshot_captures.pending || now - space_snapshot_captures.started >= SPACE_SNAPSHOT_STALE_NS) {
        generation = ++space_snapshot_captures.generation;
        space_snapshot_captures.pending = generation;
        space_snapshot_captures.started = now;
    }

    pthread_mutex_unlock(&space_snapshot_captures.lock);

    return generation;
}

static void space_snapshot_capture_end(uint64_t generation)
{
    pthread_mutex_lock(&space_snapshot_captures.lock);

    if (space_snapshot_captures.pending == generation) {
        space_snapshot_captures.pending = 0;
    }

    pthread_mutex_unlock(&space_snapshot_captures.lock);
}

static void space_snapshot_capture_release(struct space_snapshot_capture *capture)
{
    if (__sync_sub_and_fetch(&capture->references, 1)) return;
    if (capture->image) CGImageRelease(capture->image);
    dispatch_release(capture->ready);
    free(capture);
}

static CGImageRef space_snapshot_capture_image(CGRect bounds)
{
    if (@available(macOS 15.2, *)) { } else return NULL;

    uint64_t generation = space_snapshot_capture_begin();
    if (!generation) return NULL;

    struct space_snapshot_capture *capture = calloc(1, sizeof(*capture));
    if (!capture) {
        space_snapshot_capture_end(generation);
        return NULL;
    }

    capture->references = 2; // Caller and asynchronous completion.
    capture->generation = generation;
    capture->ready = dispatch_semaphore_create(0);
    if (!capture->ready) {
        free(capture);
        space_snapshot_capture_end(generation);
        return NULL;
    }

    uint64_t began = read_os_timer();
    dispatch_time_t deadline = dispatch_time(DISPATCH_TIME_NOW, SPACE_SNAPSHOT_CAPTURE_NS);
    if (@available(macOS 15.2, *)) {
        [SCScreenshotManager captureImageInRect:bounds completionHandler:^(CGImageRef image, NSError *error) {
            (void) error;
            capture->image = image ? CGImageRetain(image) : NULL;
            space_snapshot_capture_end(capture->generation);
            dispatch_semaphore_signal(capture->ready);
            space_snapshot_capture_release(capture);
        }];
    }

    CGImageRef image = NULL;
    if (dispatch_semaphore_wait(capture->ready, deadline) == 0
        && read_os_timer() - began <= SPACE_SNAPSHOT_CAPTURE_NS) {
        image = capture->image ? CGImageRetain(capture->image) : NULL;
    }
    space_snapshot_capture_release(capture);
    return image;
}

struct space_snapshot
{
    uint32_t window;
    int overlay_space;
    CGContextRef backing;
    CFStringRef uuid;
    uint64_t target;
    uint64_t started;
    uint64_t deadline;
    dispatch_source_t timer;
};

static pthread_mutex_t space_snapshot_lock = PTHREAD_MUTEX_INITIALIZER;
static struct space_snapshot *space_snapshot_active;

// The auxiliary Spaces created most recently, with space_snapshot_lock held.
// WindowServer announces each new Space to the daemon, whose handler asks for
// its type: while a switch is under way that waited 24-41 ms on the event
// loop, for every step of a burst.
#define SPACE_SNAPSHOT_RECENT_SPACES 4

static uint64_t space_snapshot_recent_spaces[SPACE_SNAPSHOT_RECENT_SPACES];
static int space_snapshot_recent_next;

static void space_snapshot_space_created(uint64_t sid)
{
    space_snapshot_recent_spaces[space_snapshot_recent_next] = sid;
    space_snapshot_recent_next = (space_snapshot_recent_next + 1) % SPACE_SNAPSHOT_RECENT_SPACES;
}

static bool space_navigation_snapshot_owns_space(uint64_t sid)
{
    bool owned = false;

    pthread_mutex_lock(&space_snapshot_lock);

    for (int i = 0; sid && i < SPACE_SNAPSHOT_RECENT_SPACES; ++i) {
        if (space_snapshot_recent_spaces[i] == sid) owned = true;
    }

    pthread_mutex_unlock(&space_snapshot_lock);

    return owned;
}

#include "space_navigation_snapshot_surface.m"

// Lock held. Timer cancellation keeps its captured pointer alive until all
// queued handlers have returned; its cancel handler owns the final free. That
// handler runs on a queue thread, without the lock, as soon as cancellation
// lets it: from dispatch_source_cancel on, the snapshot may already be freed,
// so we release the timer through a local copy.
static void space_snapshot_cancel_locked(void)
{
    struct space_snapshot *snapshot = space_snapshot_active;
    if (!snapshot) return;
    space_snapshot_active = NULL;
    space_snapshot_surface_destroy(snapshot);
    if (snapshot->window) SLSReleaseWindow(SLSMainConnectionID(), snapshot->window);
    if (snapshot->overlay_space) SLSSpaceDestroy(SLSMainConnectionID(), snapshot->overlay_space);
    if (snapshot->uuid) CFRelease(snapshot->uuid);

    dispatch_source_t timer = snapshot->timer;
    dispatch_source_cancel(timer);
    dispatch_release(timer);
}

static void space_navigation_snapshot_cancel(void)
{
    pthread_mutex_lock(&space_snapshot_lock);
    space_snapshot_cancel_locked();
    pthread_mutex_unlock(&space_snapshot_lock);
}

static void space_navigation_snapshot_space_changed(void)
{
    pthread_mutex_lock(&space_snapshot_lock);
    struct space_snapshot *snapshot = space_snapshot_active;
    if (snapshot && snapshot->started
        && SLSManagedDisplayGetCurrentSpace(SLSMainConnectionID(), snapshot->uuid) != snapshot->target) {
        space_snapshot_cancel_locked();
    }
    pthread_mutex_unlock(&space_snapshot_lock);
}

static void space_snapshot_tick(struct space_snapshot *snapshot)
{
    pthread_mutex_lock(&space_snapshot_lock);
    if (space_snapshot_active == snapshot) {
        uint64_t now = read_os_timer();
        if (now >= snapshot->deadline) {
            space_snapshot_cancel_locked();
        } else if (snapshot->started) {
            double t = (double) (now - snapshot->started) / (snapshot->deadline - snapshot->started);
            float alpha = (float) (1.0 - t * t * (3.0 - 2.0 * t));
            if (SLSSetWindowAlpha(SLSMainConnectionID(), snapshot->window, alpha) != kCGErrorSuccess) {
                space_snapshot_cancel_locked();
            }
        }
    }
    pthread_mutex_unlock(&space_snapshot_lock);
}

// Called by the event thread. Capture permission is never requested here:
// unavailable, over-budget or late captures fall back to an ordinary switch.
// One snapshot at a time bounds both the capture and overlay memory.
static bool space_navigation_snapshot_prepare(uint32_t display, uint64_t target, float interval)
{
    space_navigation_snapshot_cancel();
    if (@available(macOS 15.2, *)) { } else return false;

    uint64_t began = read_os_timer();
    if (!CGPreflightScreenCaptureAccess() || !CGDisplayIsActive(display) || !target
        || !(interval >= 1.0f / 240.0f && interval <= 1.0f)) {
        space_snapshot_trace("unavailable", began, 0);
        return false;
    }

    CGRect bounds = CGDisplayBounds(display);
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display);
    if (!mode) {
        space_snapshot_trace("unsupported display", began, 0);
        return false;
    }

    size_t width = CGDisplayModeGetPixelWidth(mode), height = CGDisplayModeGetPixelHeight(mode);
    CGDisplayModeRelease(mode);
    if (!width || !height || width > SPACE_SNAPSHOT_MAX_PIXELS / height
        || CGRectIsEmpty(bounds) || CGRectIsNull(bounds)) {
        space_snapshot_trace("unsupported display", began, 0);
        return false;
    }

    CGImageRef image = space_snapshot_capture_image(bounds);
    uint64_t captured = read_os_timer();
    if (!image) {
        space_snapshot_trace("no capture", began, captured);
        return false;
    }

    width = CGImageGetWidth(image);
    height = CGImageGetHeight(image);
    if (!width || !height || width > SPACE_SNAPSHOT_MAX_PIXELS / height
        || !CGRectEqualToRect(bounds, CGDisplayBounds(display))) {
        CGImageRelease(image);
        space_snapshot_trace("display changed", began, captured);
        return false;
    }

    struct space_snapshot *snapshot = calloc(1, sizeof(*snapshot));
    CFUUIDRef uuid = CGDisplayCreateUUIDFromDisplayID(display);
    if (!snapshot || !uuid) {
        free(snapshot);
        if (uuid) CFRelease(uuid);
        CGImageRelease(image);
        space_snapshot_trace("failed", began, captured);
        return false;
    }
    snapshot->uuid = CFUUIDCreateString(NULL, uuid);
    CFRelease(uuid);
    snapshot->target = target;
    snapshot->timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0,
                                             dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0));
    if (!snapshot->uuid || !snapshot->timer) {
        if (snapshot->uuid) CFRelease(snapshot->uuid);
        if (snapshot->timer) {
            dispatch_source_cancel(snapshot->timer);
            dispatch_resume(snapshot->timer);
            dispatch_release(snapshot->timer);
        }
        free(snapshot);
        CGImageRelease(image);
        space_snapshot_trace("failed", began, captured);
        return false;
    }

    dispatch_source_set_event_handler(snapshot->timer, ^{ @autoreleasepool { space_snapshot_tick(snapshot); } });
    dispatch_source_set_cancel_handler(snapshot->timer, ^{ free(snapshot); });
    // Before Dock replies the timer is also a watchdog: a failed or stalled
    // switch cannot leave an opaque image over the user's screen indefinitely.
    snapshot->deadline = read_os_timer() + 1000000000ULL;
    dispatch_source_set_timer(snapshot->timer, dispatch_time(DISPATCH_TIME_NOW, (uint64_t) (interval * 1e9)),
                              (uint64_t) (interval * 1e9), 1000000ULL);

    pthread_mutex_lock(&space_snapshot_lock);
    space_snapshot_active = snapshot;
    dispatch_resume(snapshot->timer);
    int cid = SLSMainConnectionID();
    CFTypeRef region = NULL;
    CFTypeRef empty = CGRegionCreateEmptyRegion();
    CGSNewRegionWithRect(&bounds, &region);
    // No activation or mouse events. An auxiliary Space, rather than the
    // sticky tag, keeps the image visible while Dock hides the source Space.
    uint64_t tags = (1ULL << 1) | (1ULL << 9);
    bool success = region && empty && snapshot->uuid
        && SLSNewWindowWithOpaqueShapeAndContext(cid, 2, region, empty, 13, &tags, 0, 0, 64,
                                                &snapshot->window, NULL) == kCGErrorSuccess
        && snapshot->window;
    if (region) CFRelease(region);
    if (empty) CFRelease(empty);
    if (success) {
        success = SLSSetWindowResolution(cid, snapshot->window, width / bounds.size.width) == kCGErrorSuccess
            && SLSSetWindowOpacity(cid, snapshot->window, false) == kCGErrorSuccess
            && SLSSetWindowLevel(cid, snapshot->window, 1) == kCGErrorSuccess;
    }
    if (success) {
        success = space_snapshot_surface_create(snapshot, image, bounds)
            && space_snapshot_space_create(snapshot)
            && SLSOrderWindow(cid, snapshot->window, 1, 0) == kCGErrorSuccess;
    }
    CGImageRelease(image);
    if (!success) space_snapshot_cancel_locked();
    pthread_mutex_unlock(&space_snapshot_lock);

    space_snapshot_trace(success ? "ready" : "failed", began, captured);

    // Allow one refresh for the outgoing image before hiding the source.
    // This is a bounded presentation opportunity, not a presentation fence.
    if (success) usleep((useconds_t) (fminf(interval, 1.0f / 30.0f) * 1e6f));
    return success;
}

static bool space_navigation_snapshot_start(float duration, bool switched)
{
    pthread_mutex_lock(&space_snapshot_lock);
    struct space_snapshot *snapshot = space_snapshot_active;
    bool active = snapshot && switched && duration > 0.0f && duration <= 1.0f;
    if (active) {
        snapshot->started = read_os_timer();
        snapshot->deadline = snapshot->started + (uint64_t) (duration * 1e9);
    } else {
        space_snapshot_cancel_locked();
    }
    pthread_mutex_unlock(&space_snapshot_lock);
    return active;
}
