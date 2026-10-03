// Crossfade one already-composited outgoing frame, or fade a solid veil, over an
// ordinary Desktop switch. Space alpha affects shared Finder windows and Space
// levels reorder global windows; this path changes only a window owned by yabai.
#include <os/signpost.h>

#define SPACE_SNAPSHOT_MAX_PIXELS 24000000ULL

// The veil's opacity. Perceived the same from 0.3 to 0.5 on this host's displays; it
// only has to hide the switch.
#define SPACE_SNAPSHOT_VEIL_OPACITY 0.4f

// The blurred veil (config navigation_veil_blur): the window's alpha carries the
// blur, so its black fill is a light tint that lets the blurred Desktop show
// through, and the window fades in over this long, with a quadratic ease-out, before
// the switch. Blurring at full strength at once was seen as a pop, and the
// destination's brightness change as a jump.
#define SPACE_SNAPSHOT_VEIL_BLUR_TINT  0.25
#define SPACE_SNAPSHOT_VEIL_BLUR_IN_NS 100000000ULL

// Signposts in subsystem com.lcs.yabai, category effects: how long each snapshot
// took to capture and to prepare, and why one was not used.
static os_log_t space_snapshot_log(void)
{
    static os_log_t log;
    static dispatch_once_t once;

    dispatch_once(&once, ^{
        log = os_log_create("com.lcs.yabai", "effects");
    });

    return log;
}

// Local measurement build only. CLOCK_UPTIME_RAW nanoseconds are included in each
// event so callback, event-loop and recorder clocks align directly.
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
    float peak;                 // Opacity at the start of the fade: 1 for a capture.
    int curve;                  // enum space_snapshot_curve, fixed when the overlay is created.
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

// The asynchronous request waiting for its capture. Only the event loop touches it.
static struct space_snapshot_request space_snapshot_pending;

// The auxiliary Spaces created most recently, with space_snapshot_lock held.
// WindowServer announces each new Space to the daemon, whose handler asks for its
// type: while a switch is under way that waited 24-41 ms on the event loop, for
// every step of a burst.
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

// Lock held. Timer cancellation keeps its captured pointer alive until all queued
// handlers have returned; its cancel handler owns the final free. That handler runs
// on a queue thread, without the lock, as soon as cancellation lets it: from
// dispatch_source_cancel on, the snapshot may already be freed, so we release the
// timer through a local copy.
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

// Event loop. Ends the overlay and a capture still waited for; the step that waits
// for that capture learns so when it asks to present it.
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

// The share of its peak opacity the overlay keeps at fade progress t in [0, 1].
// Smoothstep starts slowly: 5% of the change takes 13.5% of the duration. The
// ease-out curve changes visibly from the first frame (2.5%), so the switch reads as
// answered sooner; the fade's duration is the same.
static float space_snapshot_alpha(double t, int curve)
{
    if (t <= 0.0) return 1.0f;
    if (t >= 1.0) return 0.0f;
    if (curve == SPACE_SNAPSHOT_CURVE_EASE_OUT) return (float) ((1.0 - t) * (1.0 - t));

    return (float) (1.0 - t * t * (3.0 - 2.0 * t));
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
            float alpha = snapshot->peak * space_snapshot_alpha(t, snapshot->curve);
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

// Event loop. A snapshot with its display, target and timer, which does not run yet.
// The timer is also a watchdog before Dock replies: a failed or stalled switch
// cannot leave an opaque overlay over the user's screen indefinitely. The curve is
// read here, on the event loop, so the timer's queue never touches the
// configuration. NULL when nothing could be allocated.
static struct space_snapshot *space_snapshot_create(uint32_t display, uint64_t target, float interval, float peak)
{
    struct space_snapshot *snapshot = calloc(1, sizeof(*snapshot));
    CFUUIDRef uuid = CGDisplayCreateUUIDFromDisplayID(display);
    if (!snapshot || !uuid) {
        free(snapshot);
        if (uuid) CFRelease(uuid);
        return NULL;
    }
    snapshot->uuid = CFUUIDCreateString(NULL, uuid);
    CFRelease(uuid);
    snapshot->target = target;
    snapshot->peak = peak;
    snapshot->curve = g_window_manager.navigation_fade_curve;
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
        return NULL;
    }

    dispatch_source_set_event_handler(snapshot->timer, ^{ @autoreleasepool { space_snapshot_tick(snapshot); } });
    dispatch_source_set_cancel_handler(snapshot->timer, ^{ free(snapshot); });
    snapshot->deadline = read_os_timer() + 1000000000ULL;
    dispatch_source_set_timer(snapshot->timer, dispatch_time(DISPATCH_TIME_NOW, (uint64_t) (interval * 1e9)),
                              (uint64_t) (interval * 1e9), 1000000ULL);

    return snapshot;
}

// Lock held. The overlay's window, not yet drawn into or ordered in, whose backing
// is `resolution` pixels per point. No activation or mouse events. An auxiliary
// Space, rather than the sticky tag, keeps the overlay visible while Dock hides the
// source Space.
static bool space_snapshot_window_create(struct space_snapshot *snapshot, CGRect bounds, double resolution)
{
    int cid = SLSMainConnectionID();
    CFTypeRef region = NULL;
    CFTypeRef empty = CGRegionCreateEmptyRegion();
    CGSNewRegionWithRect(&bounds, &region);
    uint64_t tags = (1ULL << 1) | (1ULL << 9);
    bool success = region && empty
        && SLSNewWindowWithOpaqueShapeAndContext(cid, 2, region, empty, 13, &tags, 0, 0, 64,
                                                &snapshot->window, NULL) == kCGErrorSuccess
        && snapshot->window;
    if (region) CFRelease(region);
    if (empty) CFRelease(empty);
    if (success) {
        success = SLSSetWindowResolution(cid, snapshot->window, resolution) == kCGErrorSuccess
            && SLSSetWindowOpacity(cid, snapshot->window, false) == kCGErrorSuccess
            && SLSSetWindowLevel(cid, snapshot->window, 1) == kCGErrorSuccess;
    }

    return success;
}

// Event loop. Capture permission is never requested here: unavailable, over-budget
// or late captures fall back to an ordinary switch. One snapshot at a time bounds
// both the capture and overlay memory. False when no capture started.
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

// Event loop. Shows the captured image in our overlay window, ready to fade, and
// releases the request's capture. False when there is nothing to show.
static bool space_snapshot_request_finish(struct space_snapshot_request *request, bool wait)
{
    uint32_t display = request->display;
    uint64_t target = request->target;
    float interval = request->interval;
    CGRect bounds = request->bounds;
    uint64_t began = request->began;

    // An asynchronous request reports when the image arrived, not when the event
    // loop came to it.
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

    struct space_snapshot *snapshot = space_snapshot_create(display, target, interval, 1.0f);
    if (!snapshot) {
        CGImageRelease(image);
        space_snapshot_trace("failed", began, captured);
        return false;
    }
#ifdef YABAI_CAPTURE_DIAGNOSTICS
    snapshot->generation = generation;
#endif

    pthread_mutex_lock(&space_snapshot_lock);
    space_snapshot_active = snapshot;
    dispatch_resume(snapshot->timer);
    int cid = SLSMainConnectionID();
    bool success = space_snapshot_window_create(snapshot, bounds, width / bounds.size.width);
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

    // Allow one refresh for the outgoing image before hiding the source. This is a
    // bounded presentation opportunity, not a presentation fence. Local probes can
    // compile it out to check whether the switch then exposes the destination before
    // the overlay.
#ifndef SPACE_SNAPSHOT_PRESWITCH_WAIT
#define SPACE_SNAPSHOT_PRESWITCH_WAIT 1
#endif
    if (success && SPACE_SNAPSHOT_PRESWITCH_WAIT) {
        SNAP_DIAG("preswitch_wait_begin", generation, token);
        usleep((useconds_t) (fminf(interval, 1.0f / 30.0f) * 1e6f));
        SNAP_DIAG("preswitch_wait_end", generation, token);
    }
    return success;
}

// Event loop: captures the display and shows the image before returning. The capture
// can hold the event loop for SPACE_SNAPSHOT_CAPTURE_NS.
static bool space_navigation_snapshot_prepare(uint32_t display, uint64_t target, float interval)
{
    struct space_snapshot_request request;
    if (!space_snapshot_request_start(&request, display, target, interval, 0)) return false;

    return space_snapshot_request_finish(&request, true);
}

// Event loop: requests the capture and returns. The callback, and the deadline in
// any case, call space_navigation_snapshot_captured(token); the first of them to
// reach the event loop presents the snapshot. False when no capture started.
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

// Event loop: relinquish presentation without drawing or waiting. The callback's
// reference and unresolved admission slot belong to the framework request until its
// callback arrives; releasing this reference cancels neither. Do not touch an
// already visible overlay before the logical switch.
static enum space_snapshot_result space_navigation_snapshot_discard(int token)
{
    if (!space_snapshot_pending.capture || space_snapshot_pending.token != token) return SPACE_SNAPSHOT_CANCELLED;

    struct space_snapshot_capture *capture = space_snapshot_pending.capture;
    SNAP_DIAG("capture_discard", capture->generation, token);
    space_snapshot_pending = (struct space_snapshot_request) { 0 };
    space_snapshot_capture_release(capture);
    return SPACE_SNAPSHOT_MISSING;
}

// The share of its final alpha the blurred veil's window has u in [0, 1] of the way
// through its fade-in: a quadratic ease-out, fastest at the start.
static float space_snapshot_blur_alpha(double u)
{
    if (u <= 0.0) return 0.0f;
    if (u >= 1.0) return 1.0f;

    return (float) (1.0 - (1.0 - u) * (1.0 - u));
}

// Event loop. Fades the blurred veil's window in from alpha 0 to exactly 1, one
// write per refresh, each under the lock and only while this snapshot is still the
// active one. False when it was cancelled meanwhile or a write failed; nothing is
// left alive then. The snapshot may be freed once it is cancelled, so it is
// dereferenced only while it is the active one.
static bool space_snapshot_blur_in(struct space_snapshot *snapshot, float interval)
{
    uint64_t began = read_os_timer();
    int cid = SLSMainConnectionID();

    for (;;) {
        // A refresh is at most 1 s (see the callers' range check), which would
        // outlast the watchdog: cap each sleep like the wait for the order-in.
        usleep((useconds_t) (fminf(interval, 1.0f / 30.0f) * 1e6f));
        uint64_t elapsed = read_os_timer() - began;
        bool done = elapsed >= SPACE_SNAPSHOT_VEIL_BLUR_IN_NS;
        float alpha = space_snapshot_blur_alpha(done ? 1.0 : (double) elapsed / SPACE_SNAPSHOT_VEIL_BLUR_IN_NS);

        pthread_mutex_lock(&space_snapshot_lock);
        bool written = space_snapshot_active == snapshot
            && SLSSetWindowAlpha(cid, snapshot->window, alpha) == kCGErrorSuccess;
        if (!written) space_snapshot_cancel_locked();
        pthread_mutex_unlock(&space_snapshot_lock);

        if (!written) return false;
        if (done) return true;
    }
}

// Event loop: shows a solid black veil over the display, ready to fade, and returns
// once it has had two refreshes to reach the screen. It captures nothing, so it
// needs neither Screen Recording permission nor macOS 26, and costs one window
// order-in. With a background blur configured and Reduce Transparency off, the veil
// is a blurred, lightly tinted window that fades in before it returns. False when no
// veil is shown.
static bool space_navigation_veil_prepare(uint32_t display, uint64_t target, float interval)
{
    space_navigation_snapshot_cancel();

    uint64_t began = read_os_timer();
    SNAP_DIAG("veil_begin", 0, 0);
    if (!CGDisplayIsActive(display) || !target || !(interval >= 1.0f / 240.0f && interval <= 1.0f)) {
        space_snapshot_trace("veil unavailable", began, 0);
        return false;
    }

    CGRect bounds = CGDisplayBounds(display);
    if (CGRectIsEmpty(bounds) || CGRectIsNull(bounds)) {
        space_snapshot_trace("veil unavailable", began, 0);
        return false;
    }

    // Read here, on the event loop, like the curve.
    int radius = g_window_manager.navigation_veil_blur;
    bool blur = radius > 0 && !space_navigation_reduce_transparency();
    const char *shown = blur ? "veil blur" : "veil";
    const char *failed = blur ? "veil blur failed" : "veil failed";

    struct space_snapshot *snapshot = space_snapshot_create(display, target, interval,
                                                            blur ? 1.0f : SPACE_SNAPSHOT_VEIL_OPACITY);
    if (!snapshot) {
        space_snapshot_trace(failed, began, 0);
        return false;
    }

    pthread_mutex_lock(&space_snapshot_lock);
    space_snapshot_active = snapshot;
    dispatch_resume(snapshot->timer);
    int cid = SLSMainConnectionID();
    // One backing pixel per point: the veil has no detail to resolve. The blurred
    // veil starts at alpha 0 and is ordered in invisible, so that the blur does not
    // appear at full strength in one frame.
    bool success = space_snapshot_window_create(snapshot, bounds, 1.0)
        && (!blur || SLSSetWindowBackgroundBlurRadiusStyle(cid, snapshot->window, radius, 1) == kCGErrorSuccess)
        && space_snapshot_surface_fill(snapshot, bounds, blur ? SPACE_SNAPSHOT_VEIL_BLUR_TINT : 1.0)
        && SLSSetWindowAlpha(cid, snapshot->window, blur ? 0.0f : SPACE_SNAPSHOT_VEIL_OPACITY) == kCGErrorSuccess
        && space_snapshot_space_create(snapshot)
        && SLSOrderWindow(cid, snapshot->window, 1, 0) == kCGErrorSuccess;
    if (!success) space_snapshot_cancel_locked();
    pthread_mutex_unlock(&space_snapshot_lock);
    SNAP_DIAG("veil_order_end", 0, 0);

    // The blur reaches the screen later than a plain veil and competes with Dock for
    // the GPU: with it at full strength only after the switch began, Dock took about
    // twice as long and the veil once arrived later than the two refreshes below. So
    // it is shown completely before the switch. That holds the event loop for about
    // 130 ms at 60 Hz, which is why the blur is opt-in.
    if (success && blur) {
        success = space_snapshot_blur_in(snapshot, interval);
        SNAP_DIAG("veil_blur_in_end", 0, 0);
    }

    space_snapshot_trace(success ? shown : failed, began, 0);

    // A new window on a new auxiliary Space can be presented after Dock has
    // switched: switching straight after the order showed the destination unveiled
    // in 2 of 6 trials, and waiting 17 or 34 ms in none of 18. Two refreshes cover
    // every flash seen. Still not a presentation fence.
    if (success) {
        usleep((useconds_t) (fminf(2.0f * interval, 1.0f / 30.0f) * 1e6f));
        SNAP_DIAG("veil_wait_end", 0, 0);
    }

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
        SNAP_DIAG("alpha_schedule", snapshot->generation, 0);
    } else {
        space_snapshot_cancel_locked();
    }
    pthread_mutex_unlock(&space_snapshot_lock);
    return active;
}
