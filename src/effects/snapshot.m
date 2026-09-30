// Crossfade one already-composited outgoing frame over an ordinary Desktop
// switch. Space alpha affects shared Finder windows and Space levels reorder
// global windows; this path changes only a window owned by yabai.
#include <os/signpost.h>

#define SPACE_SNAPSHOT_MAX_PIXELS 24000000ULL

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

// Local measurement build only. CLOCK_UPTIME_RAW nanoseconds are included in
// each event so callback, event-loop and recorder clocks align directly.
#ifdef YABAI_CAPTURE_DIAGNOSTICS
#define SNAP_DIAG(phase, generation, token) \
    os_signpost_event_emit(space_snapshot_log(), OS_SIGNPOST_ID_EXCLUSIVE, "diag", \
                           "%{public}s gen %llu token %d ns %llu", phase, \
                           (unsigned long long)(generation), (int)(token), \
                           (unsigned long long)clock_gettime_nsec_np(CLOCK_UPTIME_RAW))
#else
#define SNAP_DIAG(phase, generation, token) ((void)0)
#endif

static void space_snapshot_trace(const char *result, uint64_t began, uint64_t captured)
{
    uint64_t now = read_os_timer();
    double capture = captured ? (captured - began) / 1e6 : 0.0;

    os_signpost_event_emit(space_snapshot_log(), OS_SIGNPOST_ID_EXCLUSIVE, "snapshot",
                           "%{public}s capture %.1f ms prepare %.1f ms", result, capture, (now - began) / 1e6);
}

#include "snapshot_capture.m"

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
#ifdef YABAI_CAPTURE_DIAGNOSTICS
    uint64_t generation;
    bool first_alpha_logged;
#endif
};

static pthread_mutex_t space_snapshot_lock = PTHREAD_MUTEX_INITIALIZER;
static struct space_snapshot *space_snapshot_active;

#ifdef YABAI_CAPTURE_DIAGNOSTICS
static uint64_t space_snapshot_diag_active_generation(void)
{
    pthread_mutex_lock(&space_snapshot_lock);
    uint64_t generation = space_snapshot_active ? space_snapshot_active->generation : 0;
    pthread_mutex_unlock(&space_snapshot_lock);
    return generation;
}
#endif

// A snapshot between its capture request and its overlay.
struct space_snapshot_request
{
    uint32_t display;
    uint64_t target;
    float interval;
    CGRect bounds;
    uint64_t began;
    int token;
    struct space_snapshot_capture *capture;
};

// The asynchronous request waiting for its capture. Only the event loop
// touches it.
static struct space_snapshot_request space_snapshot_pending;

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

bool space_navigation_snapshot_owns_space(uint64_t sid)
{
    bool owned = false;

    pthread_mutex_lock(&space_snapshot_lock);

    for (int i = 0; sid && i < SPACE_SNAPSHOT_RECENT_SPACES; ++i) {
        if (space_snapshot_recent_spaces[i] == sid) owned = true;
    }

    pthread_mutex_unlock(&space_snapshot_lock);

    return owned;
}

#include "snapshot_surface.m"

// Lock held. Timer cancellation keeps its captured pointer alive until all
// queued handlers have returned; its cancel handler owns the final free. That
// handler runs on a queue thread, without the lock, as soon as cancellation
// lets it: from dispatch_source_cancel on, the snapshot may already be freed,
// so we release the timer through a local copy.
static void space_snapshot_cancel_locked(void)
{
    struct space_snapshot *snapshot = space_snapshot_active;
    if (!snapshot) return;
#ifdef YABAI_CAPTURE_DIAGNOSTICS
    uint64_t generation = snapshot->generation;
#endif
    SNAP_DIAG("teardown_begin", generation, 0);
    space_snapshot_active = NULL;
    space_snapshot_surface_destroy(snapshot);
    if (snapshot->window) SLSReleaseWindow(SLSMainConnectionID(), snapshot->window);
    if (snapshot->overlay_space) SLSSpaceDestroy(SLSMainConnectionID(), snapshot->overlay_space);
    if (snapshot->uuid) CFRelease(snapshot->uuid);

    dispatch_source_t timer = snapshot->timer;
    dispatch_source_cancel(timer);
    dispatch_release(timer);
    SNAP_DIAG("teardown_end", generation, 0);
}

// Event loop. Ends the overlay and a capture still waited for; the step that
// waits for that capture learns so when it asks to present it.
void space_navigation_snapshot_cancel(void)
{
    pthread_mutex_lock(&space_snapshot_lock);
    space_snapshot_cancel_locked();
    pthread_mutex_unlock(&space_snapshot_lock);

    if (space_snapshot_pending.capture) space_snapshot_capture_release(space_snapshot_pending.capture);
    space_snapshot_pending = (struct space_snapshot_request) { 0 };
}

void space_navigation_snapshot_space_changed(void)
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
#ifdef YABAI_CAPTURE_DIAGNOSTICS
            else if (!snapshot->first_alpha_logged) {
                snapshot->first_alpha_logged = true;
                SNAP_DIAG("first_alpha", snapshot->generation, 0);
            }
#endif
        }
    }
    pthread_mutex_unlock(&space_snapshot_lock);
}

// Event loop. Capture permission is never requested here: unavailable,
// over-budget or late captures fall back to an ordinary switch. One snapshot
// at a time bounds both the capture and overlay memory. False when no capture
// started.
static bool space_snapshot_request_start(struct space_snapshot_request *request, uint32_t display,
                                         uint64_t target, float interval, int token)
{
    space_navigation_snapshot_cancel();
    if (@available(macOS 26.0, *)) { } else return false;

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

    struct space_snapshot_capture *capture = space_snapshot_capture_start(bounds, width, height, token);
    if (!capture) {
        space_snapshot_trace("no capture", began, read_os_timer());
        return false;
    }

    *request = (struct space_snapshot_request) {
        .display = display,
        .target = target,
        .interval = interval,
        .bounds = bounds,
        .began = began,
        .token = token,
        .capture = capture
    };

    return true;
}

// Event loop. Shows the captured image in our overlay window, ready to fade,
// and releases the request's capture. False when there is nothing to show.
static bool space_snapshot_request_finish(struct space_snapshot_request *request, bool wait)
{
    uint32_t display = request->display;
    uint64_t target = request->target;
    float interval = request->interval;
    CGRect bounds = request->bounds;
    uint64_t began = request->began;

    // An asynchronous request reports when the image arrived, not when the
    // event loop came to it.
    struct space_snapshot_capture *capture = request->capture;
#ifdef YABAI_CAPTURE_DIAGNOSTICS
    uint64_t generation = capture->generation;
    int token = capture->token;
#endif
    CGImageRef image = space_snapshot_capture_take(capture, wait);
    SNAP_DIAG("event_loop_take", generation, token);
    uint64_t captured = image && !wait ? capture->arrived : read_os_timer();
    space_snapshot_capture_release(capture);
    request->capture = NULL;

    if (!image) {
        space_snapshot_trace("no capture", began, captured);
        return false;
    }

    size_t width = CGImageGetWidth(image);
    size_t height = CGImageGetHeight(image);
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
#ifdef YABAI_CAPTURE_DIAGNOSTICS
    snapshot->generation = generation;
#endif
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
        SNAP_DIAG("draw_begin", generation, token);
        success = space_snapshot_surface_create(snapshot, image, bounds);
        SNAP_DIAG("draw_end", generation, token);
        if (success) {
            SNAP_DIAG("overlay_begin", generation, token);
            success = space_snapshot_space_create(snapshot);
            SNAP_DIAG("overlay_end", generation, token);
        }
        if (success) {
            SNAP_DIAG("order_begin", generation, token);
            success = SLSOrderWindow(cid, snapshot->window, 1, 0) == kCGErrorSuccess;
            SNAP_DIAG("order_end", generation, token);
        }
    }
    CGImageRelease(image);
    if (!success) space_snapshot_cancel_locked();
    pthread_mutex_unlock(&space_snapshot_lock);

    space_snapshot_trace(success ? "ready" : "failed", began, captured);

    // Allow one refresh for the outgoing image before hiding the source.
    // This is a bounded presentation opportunity, not a presentation fence.
    if (success) {
        SNAP_DIAG("preswitch_wait_begin", generation, token);
        usleep((useconds_t) (fminf(interval, 1.0f / 30.0f) * 1e6f));
        SNAP_DIAG("preswitch_wait_end", generation, token);
    }
    return success;
}

// Event loop: captures the display and shows the image before returning. The
// capture can hold the event loop for SPACE_SNAPSHOT_CAPTURE_NS.
static bool space_navigation_snapshot_prepare(uint32_t display, uint64_t target, float interval)
{
    struct space_snapshot_request request;
    if (!space_snapshot_request_start(&request, display, target, interval, 0)) return false;

    return space_snapshot_request_finish(&request, true);
}

// Event loop: requests the capture and returns. The callback, and the
// deadline in any case, call space_navigation_snapshot_captured(token); the
// first of them to reach the event loop presents the snapshot. False when no
// capture started.
static bool space_navigation_snapshot_capture(uint32_t display, uint64_t target, float interval, int token)
{
    if (!space_snapshot_request_start(&space_snapshot_pending, display, target, interval, token)) return false;

    dispatch_after(space_snapshot_pending.capture->deadline, dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
        space_navigation_snapshot_captured(token);
    });

    return true;
}

// Event loop: shows the snapshot the request for `token` captured.
static enum space_snapshot_result space_navigation_snapshot_present(int token)
{
    if (!space_snapshot_pending.capture || space_snapshot_pending.token != token) return SPACE_SNAPSHOT_CANCELLED;

    struct space_snapshot_request request = space_snapshot_pending;
    space_snapshot_pending = (struct space_snapshot_request) { 0 };

    return space_snapshot_request_finish(&request, false) ? SPACE_SNAPSHOT_READY : SPACE_SNAPSHOT_MISSING;
}

// Event loop: relinquish presentation without drawing or waiting. The
// callback's reference and unresolved admission slot belong to the framework
// request until its callback arrives; releasing this reference cancels neither.
// Do not touch an already visible overlay before the logical switch.
static enum space_snapshot_result space_navigation_snapshot_discard(int token)
{
    if (!space_snapshot_pending.capture || space_snapshot_pending.token != token) return SPACE_SNAPSHOT_CANCELLED;

    struct space_snapshot_capture *capture = space_snapshot_pending.capture;
    SNAP_DIAG("capture_discard", capture->generation, token);
    space_snapshot_pending = (struct space_snapshot_request) { 0 };
    space_snapshot_capture_release(capture);
    return SPACE_SNAPSHOT_MISSING;
}

static bool space_navigation_snapshot_start(float duration, bool switched)
{
    pthread_mutex_lock(&space_snapshot_lock);
    struct space_snapshot *snapshot = space_snapshot_active;
    bool active = snapshot && switched && duration > 0.0f && duration <= 1.0f;
    if (active) {
        snapshot->started = read_os_timer();
        snapshot->deadline = snapshot->started + (uint64_t) (duration * 1e9);
        SNAP_DIAG("alpha_schedule", snapshot->generation, 0);
    } else {
        space_snapshot_cancel_locked();
    }
    pthread_mutex_unlock(&space_snapshot_lock);
    return active;
}
