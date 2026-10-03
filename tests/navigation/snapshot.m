// Exercise the actual asynchronous renderer with in-memory window/capture
// dependencies. No real Desktop is captured and no window is opened.
#import <Cocoa/Cocoa.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>

// The test runs its 26+ mock only after the runtime guard in main. Keep the target
// at macOS 11 so the unsupported-system fallback can also be checked.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunguarded-availability-new"

static uint64_t read_os_timer(void)
{
    return clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
}

static int live_windows, live_spaces, alpha_writes, color_space_writes;
// What the veil asks of the window, in the order WindowServer sees it.
static double last_resolution;
static float last_alpha, alpha_at_order, max_alpha;
static int alpha_rises;
static int orders;
static CGContextRef last_context;
static int context_color_space_writes;
static bool deny_capture, fail_create, fail_context, fail_order, fail_attach, fail_alpha;
// The blurred veil: what it asked of the blur, the alphas it wrote in order, and
// hooks that fail or cancel it at a given alpha write (1-based, 0 = off).
static bool fail_blur, reduce_transparency_on;
static int blur_calls, last_blur_radius, last_blur_style, orders_at_blur;
#define ALPHA_LOG_MAX 512
static float alpha_log[ALPHA_LOG_MAX];
static int alpha_log_count;
static int fail_at_alpha_write, cancel_at_alpha_write;
static bool wrong_capture_size;
static uint64_t capture_delay, capture_start_delay;
static uint64_t active_sid = 2;
static bool overlay_attached;
static CGImageRef fixture_image;
static CGImageRef wrong_image;
static CGRect fixture_bounds = {{0, 0}, {16, 16}};

// Captures whose callbacks do not arrive until the test delivers them.
static bool capture_lost;
static void (^lost_handlers[16])(SCScreenshotOutput *, NSError *);
static int lost_count;
static int modern_capture_calls, legacy_capture_calls;

@interface SnapshotOutputMock : NSObject
- (CGImageRef)sdrImage;
@end
@implementation SnapshotOutputMock
- (CGImageRef)sdrImage { return wrong_capture_size ? wrong_image : fixture_image; }
@end

@interface SnapshotCaptureMock : NSObject
+ (void)captureImageInRect:(CGRect)rect completionHandler:(void (^)(CGImageRef, NSError *))handler;
+ (void)captureScreenshotWithRect:(CGRect)rect configuration:(SCScreenshotConfiguration *)config
               completionHandler:(void (^)(SCScreenshotOutput *, NSError *))handler;
@end
@implementation SnapshotCaptureMock
+ (void)captureImageInRect:(CGRect)rect completionHandler:(void (^)(CGImageRef, NSError *))handler
{
    (void)rect;
    ++legacy_capture_calls;
    (void)handler;
    assert(!"legacy rectangle capture must not run");
}
+ (void)captureScreenshotWithRect:(CGRect)rect configuration:(SCScreenshotConfiguration *)config
               completionHandler:(void (^)(SCScreenshotOutput *, NSError *))handler
{
    assert(CGRectEqualToRect(rect, fixture_bounds));
    assert(config.dynamicRange == SCScreenshotDynamicRangeSDR);
    assert(config.displayIntent == SCScreenshotDisplayIntentLocal);
    assert(config.width == 16 && config.height == 16 && config.showsCursor);
    ++modern_capture_calls;
    if (capture_lost) {
        assert(lost_count < (int)(sizeof(lost_handlers) / sizeof(lost_handlers[0])));
        lost_handlers[lost_count++] = Block_copy(handler);
        return;
    }

    bool denied = deny_capture;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, capture_delay), dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{
        SnapshotOutputMock *output = denied ? nil : [[SnapshotOutputMock alloc] init];
        handler((SCScreenshotOutput *)output, nil);
        [output release];
    });
    if (capture_start_delay) usleep((useconds_t)(capture_start_delay / 1000));
}
@end

static int SLSMainConnectionID(void)
{
    return 1;
}

static CGError CGSNewRegionWithRect(CGRect *r, CFTypeRef *out)
{
    (void)r;
    *out = CFRetain(kCFBooleanTrue);
    return 0;
}

static CFTypeRef CGRegionCreateEmptyRegion(void)
{
    return CFRetain(kCFBooleanFalse);
}

static CGError SLSNewWindowWithOpaqueShapeAndContext(int c, int t, CFTypeRef r, CFTypeRef e, int o, uint64_t *tags,
                                                     float x, float y, int n, uint32_t *w, void *ctx)
{
    (void)c;
    (void)t;
    (void)r;
    (void)e;
    (void)o;
    (void)x;
    (void)y;
    (void)n;
    (void)ctx;
    assert(!(*tags & (1ULL << 11))); // Sticky source membership must not return.
    assert(*tags & (1ULL << 9)); // Overlay must never intercept clicks.
    if (fail_create) return kCGErrorFailure;
    color_space_writes = 0;
    last_resolution = 0;
    last_alpha = -1.0f;
    *w = 77;
    __atomic_add_fetch(&live_windows, 1, __ATOMIC_SEQ_CST);
    return 0;
}

static CGError SLSSetWindowResolution(int c, uint32_t w, double s)
{
    (void)c;
    (void)w;
    last_resolution = s;
    return 0;
}

static CGError SLSSetWindowOpacity(int c, uint32_t w, bool opaque)
{
    (void)c;
    (void)w;
    (void)opaque;
    return 0;
}

static CGError SLSSetWindowLevel(int c, uint32_t w, int level)
{
    (void)c;
    (void)w;
    assert(level == 1);
    return 0;
}

// Called with space_snapshot_lock held, like the writes it stands for.
static void space_snapshot_cancel_locked(void);

static CGError SLSSetWindowAlpha(int c, uint32_t w, float alpha)
{
    (void)c;
    (void)w;
    assert(alpha >= 0 && alpha <= 1);
    if (last_alpha >= 0 && alpha > last_alpha) ++alpha_rises;
    if (alpha > max_alpha) max_alpha = alpha;
    last_alpha = alpha;
    if (alpha_log_count < ALPHA_LOG_MAX) alpha_log[alpha_log_count] = alpha;
    ++alpha_log_count;
    __atomic_add_fetch(&alpha_writes, 1, __ATOMIC_SEQ_CST);
    if (cancel_at_alpha_write && alpha_log_count == cancel_at_alpha_write) space_snapshot_cancel_locked();
    if (fail_at_alpha_write && alpha_log_count == fail_at_alpha_write) return kCGErrorFailure;
    return fail_alpha ? kCGErrorFailure : 0;
}

static CGError SLSSetWindowBackgroundBlurRadiusStyle(int c, uint32_t w, int radius, int style)
{
    (void)c;
    assert(w == 77);
    ++blur_calls;
    last_blur_radius = radius;
    last_blur_style = style;
    orders_at_blur = orders;
    return fail_blur ? kCGErrorFailure : 0;
}

static CGError SLSReleaseWindow(int c, uint32_t w)
{
    (void)c;
    (void)w;
    assert(__atomic_sub_fetch(&live_windows, 1, __ATOMIC_SEQ_CST) == 0);
    return 0;
}

static CGError SLSOrderWindow(int c, uint32_t w, int mode, uint32_t rel)
{
    (void)c;
    (void)w;
    (void)mode;
    (void)rel;
    assert(overlay_attached); // Attach before presenting the outgoing image.
    alpha_at_order = last_alpha;
    ++orders;
    return fail_order ? kCGErrorFailure : 0;
}

static CGContextRef bitmap(void)
{
    CGColorSpaceRef colors = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(NULL, 16, 16, 8, 64, colors, (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(colors);
    return ctx;
}

// The window takes the capture's colour space before its context exists, so that
// drawing the image converts nothing.
static CGError SLSSetWindowColorSpace(int c, uint32_t w, CGColorSpaceRef space)
{
    (void)c;
    assert(w == 77 && space == CGImageGetColorSpace(fixture_image));
    ++color_space_writes;
    return 0;
}

static CGContextRef SLWindowContextCreate(int c, uint32_t w, void *options)
{
    (void)c;
    (void)w;
    (void)options;
    context_color_space_writes = color_space_writes;
    last_context = fail_context ? NULL : bitmap();
    return last_context;
}

static bool fail_space;

static int SLSSpaceCreate(int c, int type, int options)
{
    (void)c;
    (void)options;
    assert(type == 1);
    if (fail_space) return 0;
    assert(__atomic_add_fetch(&live_spaces, 1, __ATOMIC_SEQ_CST) == 1);
    return 123;
}

static CGError SLSSpaceDestroy(int c, int sid)
{
    (void)c;
    assert(sid == 123);
    assert(__atomic_sub_fetch(&live_spaces, 1, __ATOMIC_SEQ_CST) == 0);
    overlay_attached = false;
    return 0;
}

static void SLSSpaceSetAbsoluteLevel(int c, int sid, int level)
{
    (void)c;
    assert(sid == 123 && level == 0);
}

static void SLSShowSpaces(int c, CFArrayRef spaces)
{
    (void)c;
    assert(CFArrayGetCount(spaces) == 1);
}

static void SLSSpaceAddWindowsAndRemoveFromSpaces(int c, int sid, CFArrayRef windows, int mask)
{
    (void)c;
    assert(sid == 123 && mask == 7 && CFArrayGetCount(windows) == 1);
    overlay_attached = !fail_attach;
}

static CFArrayRef SLSCopyWindowsWithOptionsAndTags(int c, uint32_t owner, CFArrayRef spaces,
                                                uint32_t options, uint64_t *set, uint64_t *clear)
{
    (void)c;
    (void)owner;
    (void)spaces;
    (void)set;
    (void)clear;
    assert(options == 7);
    int wid = overlay_attached ? 77 : 0;
    CFNumberRef number = CFNumberCreate(NULL, kCFNumberIntType, &wid);
    CFArrayRef result = CFArrayCreate(NULL, (const void **)&number, 1, &kCFTypeArrayCallBacks);
    CFRelease(number);
    return result;
}

static uint64_t SLSManagedDisplayGetCurrentSpace(int c, CFStringRef uuid)
{
    (void)c;
    (void)uuid;
    return active_sid;
}

static bool fake_access(void)
{
    return true;
}

static bool fake_active(uint32_t display)
{
    return display == 1;
}

static CGRect fake_bounds(uint32_t display)
{
    (void)display;
    return fixture_bounds;
}

static CGDisplayModeRef fake_mode(uint32_t display)
{
    (void)display;
    return (CGDisplayModeRef)1;
}

static size_t fake_pixels(CGDisplayModeRef mode)
{
    (void)mode;
    return 16;
}

static void fake_mode_release(CGDisplayModeRef mode)
{
    (void)mode;
}

static CFUUIDRef fake_uuid(uint32_t display)
{
    (void)display;
    return CFUUIDCreate(NULL);
}

// The timer's cancel handler frees the snapshot on a queue thread as soon as
// cancellation lets it run. Give it that chance before cancel returns, so a read of
// the snapshot afterwards touches freed memory. A handler that cancels its own timer
// delays its cancel handler until it returns, so waiting there proves nothing.
static void cancel_and_let_handler_run(dispatch_source_t source)
{
    dispatch_source_cancel(source);
    if (pthread_main_np()) usleep(20000);
}

// The reports of asynchronous captures: from the callback, and at the deadline.
static int captured_reports, captured_token;

static void space_navigation_snapshot_captured(int token)
{
    __atomic_store_n(&captured_token, token, __ATOMIC_SEQ_CST);
    __atomic_add_fetch(&captured_reports, 1, __ATOMIC_SEQ_CST);
}

// The fade curve and the veil's blur radius the event loop reads when it creates an
// overlay, and the Reduce Transparency setting it asks the system.
static struct { int navigation_fade_curve; int navigation_veil_blur; } g_window_manager;

static bool space_navigation_reduce_transparency(void)
{
    return reduce_transparency_on;
}

#define dispatch_source_cancel cancel_and_let_handler_run
#define SCScreenshotManager SnapshotCaptureMock
#define CGPreflightScreenCaptureAccess fake_access
#define CGDisplayIsActive fake_active
#define CGDisplayBounds fake_bounds
#define CGDisplayCopyDisplayMode fake_mode
#define CGDisplayModeGetPixelWidth fake_pixels
#define CGDisplayModeGetPixelHeight fake_pixels
#define CGDisplayModeRelease fake_mode_release
#define CGDisplayCreateUUIDFromDisplayID fake_uuid
#define SPACE_SNAPSHOT_STALE_NS 400000000ULL
#include "../../src/effects/snapshot.h"
#include "../../src/effects/snapshot.m"

static bool space_snapshot_capture_pending(void)
{
    pthread_mutex_lock(&space_snapshot_captures.lock);
    bool pending = space_snapshot_captures.pending != 0;
    pthread_mutex_unlock(&space_snapshot_captures.lock);

    return pending;
}

static unsigned space_snapshot_capture_unresolved(void)
{
    pthread_mutex_lock(&space_snapshot_captures.lock);
    unsigned unresolved = space_snapshot_captures.unresolved;
    pthread_mutex_unlock(&space_snapshot_captures.lock);
    return unresolved;
}

static bool prepare(void)
{
    return space_navigation_snapshot_prepare(1, 2, 1.0f / 120.0f);
}

static void expect_reports(int count)
{
    uint64_t end = read_os_timer() + 2000000000ULL;
    while (__atomic_load_n(&captured_reports, __ATOMIC_SEQ_CST) < count && read_os_timer() < end)
        usleep(1000);
    assert(__atomic_load_n(&captured_reports, __ATOMIC_SEQ_CST) == count);
}

static bool capture(int token)
{
    __atomic_store_n(&captured_reports, 0, __ATOMIC_SEQ_CST);
    return space_navigation_snapshot_capture(1, 2, 1.0f / 120.0f, token);
}

static void expect_released(void)
{
    uint64_t end = read_os_timer() + 2000000000ULL;
    while ((__atomic_load_n(&live_windows, __ATOMIC_SEQ_CST)
            || __atomic_load_n(&live_spaces, __ATOMIC_SEQ_CST)) && read_os_timer() < end)
        usleep(1000);
    assert(!__atomic_load_n(&live_windows, __ATOMIC_SEQ_CST));
    assert(!__atomic_load_n(&live_spaces, __ATOMIC_SEQ_CST));
}

static void deliver_lost(int index)
{
    assert(index >= 0 && index < lost_count && lost_handlers[index]);
    void (^handler)(SCScreenshotOutput *, NSError *) = lost_handlers[index];
    lost_handlers[index] = NULL;
    SnapshotOutputMock *output = [[SnapshotOutputMock alloc] init];
    handler((SCScreenshotOutput *)output, nil);
    [output release];
    Block_release(handler);
}

// Each curve starts fully opaque, ends transparent, never brightens again and stays
// within [0, 1] even when a late tick lands outside the interval. The point where 5%
// of the change becomes visible is the documented one: 13.5% of the duration for the
// smooth curve, 2.5% for ease-out.
static void expect_alpha_curve(void)
{
    double visible[SPACE_SNAPSHOT_CURVE_COUNT] = {
        [SPACE_SNAPSHOT_CURVE_SMOOTH] = 0.1354,
        [SPACE_SNAPSHOT_CURVE_EASE_OUT] = 0.0253
    };

    for (int curve = 0; curve < SPACE_SNAPSHOT_CURVE_COUNT; ++curve) {
        assert(space_snapshot_alpha(-0.5, curve) == 1.0f && space_snapshot_alpha(0.0, curve) == 1.0f);
        assert(space_snapshot_alpha(1.0, curve) == 0.0f && space_snapshot_alpha(1.5, curve) == 0.0f);
        float previous = 1.0f;
        for (int i = 1; i <= 1000; ++i) {
            float alpha = space_snapshot_alpha(i / 1000.0, curve);
            assert(alpha >= 0.0f && alpha <= previous);
            previous = alpha;
        }
        assert(space_snapshot_alpha(visible[curve] - 0.002, curve) > 0.95f);
        assert(space_snapshot_alpha(visible[curve] + 0.002, curve) < 0.95f);
    }

    // Ease-out is ahead of smooth early on, and an unknown value is smooth.
    assert(space_snapshot_alpha(0.1, SPACE_SNAPSHOT_CURVE_EASE_OUT) < space_snapshot_alpha(0.1, SPACE_SNAPSHOT_CURVE_SMOOTH));
    assert(space_snapshot_alpha(0.3, 99) == space_snapshot_alpha(0.3, SPACE_SNAPSHOT_CURVE_SMOOTH));

    // The names the config option accepts.
    assert(!strcmp(space_snapshot_curve_str[SPACE_SNAPSHOT_CURVE_SMOOTH], "smooth"));
    assert(!strcmp(space_snapshot_curve_str[SPACE_SNAPSHOT_CURVE_EASE_OUT], "ease_out"));
}

static bool veil(void)
{
    return space_navigation_veil_prepare(1, 2, 1.0f / 120.0f);
}

// The overlay's fields, read where the timer's queue reads them.
static void snapshot_fields(float *peak, int *curve)
{
    pthread_mutex_lock(&space_snapshot_lock);
    assert(space_snapshot_active);
    *peak = space_snapshot_active->peak;
    *curve = space_snapshot_active->curve;
    pthread_mutex_unlock(&space_snapshot_lock);
}

// The backing the veil drew: every pixel black at the given alpha byte, give or take
// the rounding of the fill alpha.
static void expect_fill(CGContextRef ctx, int alpha)
{
    assert(ctx && CGBitmapContextGetWidth(ctx) == 16 && CGBitmapContextGetHeight(ctx) == 16);
    const uint8_t *row = CGBitmapContextGetData(ctx);
    size_t stride = CGBitmapContextGetBytesPerRow(ctx);
    for (size_t y = 0; y < 16; ++y) {
        for (size_t x = 0; x < 16; ++x) {
            const uint8_t *pixel = row + y * stride + x * 4;
            assert(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0 && abs(pixel[3] - alpha) <= 1);
        }
    }
}

// The veil needs no capture and no macOS 26: it is a window, a Space and a fade, and
// every failure on the way leaves nothing behind.
static void expect_veil(void)
{
    int modern = modern_capture_calls, legacy = legacy_capture_calls;
    int orders_before = orders;
    int blurs_before = blur_calls;

    // It is shown at its opacity before it is ordered in, on a backing of one pixel
    // per point drawn without a capture's colour space, and its preparation waits
    // two refreshes (1/120 s each) before returning.
    uint64_t began = read_os_timer();
    assert(veil());
    assert(read_os_timer() - began >= 15000000ULL);
    assert(live_windows == 1 && live_spaces == 1 && orders == orders_before + 1);
    assert(last_resolution == 1.0 && alpha_at_order == SPACE_SNAPSHOT_VEIL_OPACITY);
    assert(context_color_space_writes == 0);
    expect_fill(last_context, 255);
    float peak;
    int curve;
    snapshot_fields(&peak, &curve);
    assert(peak == SPACE_SNAPSHOT_VEIL_OPACITY && curve == SPACE_SNAPSHOT_CURVE_SMOOTH);

    // The fade writes alphas that never exceed the veil's opacity or rise again, and
    // the endpoint releases the window and its Space.
    int writes = __atomic_load_n(&alpha_writes, __ATOMIC_SEQ_CST);
    max_alpha = 0.0f;
    alpha_rises = 0;
    assert(space_navigation_snapshot_start(.25f, true));
    expect_released();
    assert(__atomic_load_n(&alpha_writes, __ATOMIC_SEQ_CST) > writes + 2);
    assert(max_alpha <= SPACE_SNAPSHOT_VEIL_OPACITY && max_alpha > 0.0f && alpha_rises == 0);

    // Dock failed, or the switch was abandoned: the veil goes at once.
    assert(veil());
    assert(!space_navigation_snapshot_start(.2f, false));
    expect_released();

    // Cancelled while it waits for Dock, twice, and by leaving the target.
    assert(veil());
    space_navigation_snapshot_cancel();
    space_navigation_snapshot_cancel();
    expect_released();

    assert(veil());
    assert(space_navigation_snapshot_start(.2f, true));
    active_sid = 3;
    space_navigation_snapshot_space_changed();
    expect_released();
    active_sid = 2;

    // One overlay at a time: a new veil retires the one showing.
    assert(veil());
    assert(veil());
    assert(live_windows == 1 && live_spaces == 1);
    space_navigation_snapshot_cancel();
    expect_released();

    // An unprepared veil is a watchdog case like any overlay: Dock never replying
    // removes it.
    assert(veil());
    expect_released();

    // A failure at any step leaves no window and no Space, and orders nothing unless
    // it fails at the order itself.
    fail_create = true;
    assert(!veil());
    fail_create = false;
    expect_released();
    fail_context = true;
    assert(!veil());
    fail_context = false;
    expect_released();
    fail_alpha = true;
    orders_before = orders;
    assert(!veil()); // Never ordered in at the wrong opacity.
    assert(orders == orders_before);
    fail_alpha = false;
    expect_released();
    fail_space = true;
    assert(!veil());
    fail_space = false;
    expect_released();
    fail_attach = true;
    orders_before = orders;
    assert(!veil());
    assert(orders == orders_before);
    fail_attach = false;
    expect_released();
    fail_order = true;
    assert(!veil());
    fail_order = false;
    expect_released();

    // An inactive display, no target, a refresh interval out of range and an empty
    // display show nothing.
    assert(!space_navigation_veil_prepare(2, 2, 1.0f / 120.0f));
    assert(!space_navigation_veil_prepare(1, 0, 1.0f / 120.0f));
    assert(!space_navigation_veil_prepare(1, 2, 0.0f));
    assert(!space_navigation_veil_prepare(1, 2, 1.0f / 1000.0f));
    assert(!space_navigation_veil_prepare(1, 2, 2.0f));
    CGRect bounds = fixture_bounds;
    fixture_bounds = CGRectZero;
    assert(!veil());
    fixture_bounds = bounds;
    expect_released();

    // The curve is the configured one when the overlay is created, and changing it
    // afterwards leaves that overlay alone.
    g_window_manager.navigation_fade_curve = SPACE_SNAPSHOT_CURVE_EASE_OUT;
    assert(veil());
    snapshot_fields(&peak, &curve);
    assert(peak == SPACE_SNAPSHOT_VEIL_OPACITY && curve == SPACE_SNAPSHOT_CURVE_EASE_OUT);
    g_window_manager.navigation_fade_curve = SPACE_SNAPSHOT_CURVE_SMOOTH;
    snapshot_fields(&peak, &curve);
    assert(curve == SPACE_SNAPSHOT_CURVE_EASE_OUT);
    max_alpha = 0.0f;
    alpha_rises = 0;
    assert(space_navigation_snapshot_start(.1f, true));
    expect_released();
    assert(max_alpha <= SPACE_SNAPSHOT_VEIL_OPACITY && alpha_rises == 0);

    // Without a configured blur nothing asks WindowServer for one.
    assert(blur_calls == blurs_before);
    assert(modern_capture_calls == modern && legacy_capture_calls == legacy);
}

// The blurred veil: the plain veil's setup with a blur radius, a light tint and an
// alpha of 0 before it is ordered in, then a fade-in to exactly 1 and the usual
// two-refresh wait, all before the step switches. Reduce Transparency, a failing
// blur and a cancellation during the fade-in each end with nothing alive.
static void expect_veil_blur(void)
{
    // The fade-in's curve: a quadratic ease-out from 0 to exactly 1.
    assert(space_snapshot_blur_alpha(-1.0) == 0.0f && space_snapshot_blur_alpha(0.0) == 0.0f);
    assert(space_snapshot_blur_alpha(1.0) == 1.0f && space_snapshot_blur_alpha(2.0) == 1.0f);
    assert(fabsf(space_snapshot_blur_alpha(0.5) - 0.75f) < 1e-6f);
    float previous = 0.0f;
    for (int i = 1; i <= 1000; ++i) {
        float alpha = space_snapshot_blur_alpha(i / 1000.0);
        assert(alpha >= previous && alpha <= 1.0f);
        previous = alpha;
    }

    int orders_before = orders;
    int blurs_before = blur_calls;
    g_window_manager.navigation_veil_blur = 40;
    alpha_log_count = 0;
    uint64_t began = read_os_timer();
    assert(veil());
    uint64_t took = read_os_timer() - began;

    // The blur is asked for with the configured radius and style 1 before the window
    // is ordered in, which happens at alpha 0 and on a light tint, and the
    // preparation covers the fade-in and the two refreshes (1/120 s each).
    assert(blur_calls == blurs_before + 1 && last_blur_radius == 40 && last_blur_style == 1);
    assert(orders_at_blur == orders_before && orders == orders_before + 1);
    assert(live_windows == 1 && live_spaces == 1);
    assert(alpha_at_order == 0.0f && last_resolution == 1.0 && context_color_space_writes == 0);
    expect_fill(last_context, (int) (SPACE_SNAPSHOT_VEIL_BLUR_TINT * 255.0 + 0.5));
    assert(took >= SPACE_SNAPSHOT_VEIL_BLUR_IN_NS + 15000000ULL);

    // Alpha 0 first, then writes that never fall and end at exactly 1.
    assert(alpha_log_count >= 3 && alpha_log_count <= ALPHA_LOG_MAX);
    assert(alpha_log[0] == 0.0f);
    for (int i = 1; i < alpha_log_count; ++i)
        assert(alpha_log[i] >= alpha_log[i - 1] && alpha_log[i] <= 1.0f);
    assert(alpha_log[alpha_log_count - 1] == 1.0f);
    float peak;
    int curve;
    snapshot_fields(&peak, &curve);
    assert(peak == 1.0f && curve == SPACE_SNAPSHOT_CURVE_SMOOTH);

    // The fade after the switch runs from the full window alpha down and releases
    // the window and its Space.
    int writes = __atomic_load_n(&alpha_writes, __ATOMIC_SEQ_CST);
    max_alpha = 0.0f;
    alpha_rises = 0;
    assert(space_navigation_snapshot_start(.25f, true));
    expect_released();
    assert(__atomic_load_n(&alpha_writes, __ATOMIC_SEQ_CST) > writes + 2);
    assert(max_alpha <= 1.0f && max_alpha > 0.0f && alpha_rises == 0);

    // Dock failed: the blurred veil goes at once, like the plain one.
    assert(veil());
    assert(!space_navigation_snapshot_start(.2f, false));
    expect_released();

    // With Reduce Transparency on, the veil is the plain one.
    reduce_transparency_on = true;
    blurs_before = blur_calls;
    assert(veil());
    assert(blur_calls == blurs_before && alpha_at_order == SPACE_SNAPSHOT_VEIL_OPACITY);
    expect_fill(last_context, 255);
    snapshot_fields(&peak, &curve);
    assert(peak == SPACE_SNAPSHOT_VEIL_OPACITY);
    space_navigation_snapshot_cancel();
    expect_released();
    reduce_transparency_on = false;

    // A radius of 0 is the plain veil too, whatever Reduce Transparency says.
    g_window_manager.navigation_veil_blur = 0;
    blurs_before = blur_calls;
    assert(veil());
    assert(blur_calls == blurs_before && alpha_at_order == SPACE_SNAPSHOT_VEIL_OPACITY);
    space_navigation_snapshot_cancel();
    expect_released();
    g_window_manager.navigation_veil_blur = 40;

    // The radius is the configured one when the veil is prepared.
    g_window_manager.navigation_veil_blur = 100;
    assert(veil());
    assert(last_blur_radius == 100);
    space_navigation_snapshot_cancel();
    expect_released();
    g_window_manager.navigation_veil_blur = 40;

    // A refused blur fails the veil before anything is drawn or ordered in.
    fail_blur = true;
    orders_before = orders;
    assert(!veil());
    assert(orders == orders_before);
    fail_blur = false;
    expect_released();

    // A failed alpha write during the fade-in ends the veil.
    alpha_log_count = 0;
    fail_at_alpha_write = 3;
    assert(!veil());
    fail_at_alpha_write = 0;
    assert(alpha_log_count == 3);
    expect_released();

    // Cancelled during the fade-in: it stops writing, fails and leaves nothing
    // alive. The window had been ordered in.
    alpha_log_count = 0;
    orders_before = orders;
    cancel_at_alpha_write = 3;
    assert(!veil());
    cancel_at_alpha_write = 0;
    assert(alpha_log_count == 3 && orders == orders_before + 1);
    assert(!live_windows && !live_spaces);
    expect_released();
    assert(!space_navigation_snapshot_start(.2f, true)); // Nothing to fade.

    g_window_manager.navigation_veil_blur = 0;
}

// A veil retires the capture waiting for its step, whether the image arrived or its
// callback is still missing, and needs none of the capture admission.
static void expect_veil_retires_capture(void)
{
    capture_delay = SPACE_SNAPSHOT_CAPTURE_NS / 2;
    assert(capture(16));
    while (space_snapshot_capture_pending())
        usleep(1000);
    capture_delay = 0;
    assert(veil());
    assert(!space_snapshot_pending.capture);
    assert(space_navigation_snapshot_present(16) == SPACE_SNAPSHOT_CANCELLED);
    space_navigation_snapshot_cancel();
    expect_released();
    expect_reports(2);

    capture_lost = true;
    int lost = lost_count;
    assert(capture(17));
    assert(space_snapshot_pending.capture && space_snapshot_capture_unresolved() == 1);
    assert(veil());
    assert(!space_snapshot_pending.capture);
    assert(space_navigation_snapshot_present(17) == SPACE_SNAPSHOT_CANCELLED);
    assert(space_snapshot_capture_unresolved() == 1); // The framework's slot stays.
    space_navigation_snapshot_cancel();
    expect_released();
    deliver_lost(lost); // The callback, when it comes, finds nothing to touch.
    assert(space_snapshot_capture_unresolved() == 0 && !space_snapshot_capture_pending());
    capture_lost = false;
    expect_reports(2);
    assert(!live_windows && !live_spaces);

    // Two callbacks still missing refuse a capture; the veil is unaffected.
    capture_lost = true;
    lost = lost_count;
    assert(!prepare());
    usleep((useconds_t)(SPACE_SNAPSHOT_STALE_NS / 1000));
    assert(!prepare());
    assert(space_snapshot_capture_unresolved() == SPACE_SNAPSHOT_MAX_UNRESOLVED);
    int bound_calls = modern_capture_calls;
    assert(veil());
    space_navigation_snapshot_cancel();
    expect_released();
    assert(modern_capture_calls == bound_calls);
    deliver_lost(lost + 1);
    deliver_lost(lost);
    assert(space_snapshot_capture_unresolved() == 0);
    capture_lost = false;
}

int main(void)
{
    @autoreleasepool
    {
        expect_alpha_curve();
        expect_veil(); // No capture: this runs on every system.
        expect_veil_blur();
        if (@available(macOS 26.0, *)) { } else {
            assert(!prepare()); // Older systems switch without the crossfade.
            puts("snapshot: alpha curves, veil and blurred veil passed; the crossfade needs macOS 26");
            return 0;
        }
        CGContextRef ctx = bitmap();
        fixture_image = CGBitmapContextCreateImage(ctx);
        CGContextRelease(ctx);
        wrong_image = CGImageCreateWithImageInRect(fixture_image, CGRectMake(0, 0, 8, 8));
        // The image keeps its capture's colour space and its full opacity, and the
        // curve is the configured one at the moment it is created.
        g_window_manager.navigation_fade_curve = SPACE_SNAPSHOT_CURVE_EASE_OUT;
        assert(prepare());
        g_window_manager.navigation_fade_curve = SPACE_SNAPSHOT_CURVE_SMOOTH;
        float image_peak;
        int image_curve;
        snapshot_fields(&image_peak, &image_curve);
        assert(image_peak == 1.0f && image_curve == SPACE_SNAPSHOT_CURVE_EASE_OUT);
        assert(context_color_space_writes > 0);
        space_navigation_snapshot_cancel();
        expect_released();

        // The endpoint releases the overlay, with actual timer callbacks. The fade
        // lasts long enough for a tick to write alpha on a loaded runner: a tick
        // past the deadline only releases.
        assert(prepare());
        assert(space_navigation_snapshot_start(.25f, true));
        expect_released();
        assert(__atomic_load_n(&alpha_writes, __ATOMIC_SEQ_CST) > 0);

        assert(prepare());
        assert(!space_navigation_snapshot_start(.2f, false));
        expect_released(); // Dock failure.

        assert(prepare());
        space_navigation_snapshot_cancel();
        space_navigation_snapshot_cancel();
        expect_released(); // Idempotent cancellation before start.

        assert(prepare()); // New preparation retires a previous snapshot.
        assert(prepare());
        assert(space_navigation_snapshot_start(.2f, true));
        active_sid = 3;
        space_navigation_snapshot_space_changed();
        expect_released();
        active_sid = 2;

        fail_create = true;
        assert(!prepare());
        fail_create = false;
        expect_released();
        fail_context = true;
        assert(!prepare());
        fail_context = false;
        expect_released();
        fail_space = true;
        assert(!prepare());
        fail_space = false;
        expect_released();
        fail_attach = true;
        assert(!prepare());
        fail_attach = false;
        expect_released();
        fail_order = true;
        assert(!prepare());
        fail_order = false;
        expect_released();
        fail_alpha = true;
        assert(prepare());
        assert(space_navigation_snapshot_start(.05f, true));
        expect_released();
        fail_alpha = false;
        deny_capture = true;
        assert(!prepare());
        deny_capture = false;
        wrong_capture_size = true;
        assert(!prepare()); // A logical-size image must not be stretched to the display.
        wrong_capture_size = false;
        capture_start_delay = SPACE_SNAPSHOT_CAPTURE_NS * 2;
        assert(!prepare()); // A callback signaled during a slow API call is still too late.
        capture_start_delay = 0;

        capture_delay = SPACE_SNAPSHOT_CAPTURE_NS * 2;
        assert(!prepare()); // Timeout, then a new request while the callback is late.
        assert(!prepare());
        while (space_snapshot_capture_pending())
            usleep(1000);
        capture_delay = 0;
        assert(prepare()); // The late callback did not resurrect its image/window.
        expect_released(); // Prepared but never started: watchdog releases it.

        // A callback that never comes blocks new captures only for a while.
        capture_lost = true;
        assert(!prepare());
        capture_lost = false;
        assert(!prepare() && space_snapshot_capture_pending());
        usleep((useconds_t) (SPACE_SNAPSHOT_STALE_NS / 1000));
        assert(prepare());
        space_navigation_snapshot_cancel();
        expect_released();

        // When it finally comes, it releases only its own image and leaves a newer
        // capture in flight alone.
        capture_delay = SPACE_SNAPSHOT_CAPTURE_NS * 2;
        assert(!prepare() && space_snapshot_capture_pending());
        deliver_lost(0);
        assert(space_snapshot_capture_pending());
        while (space_snapshot_capture_pending())
            usleep(1000);
        capture_delay = 0;

        // Two callbacks may remain missing across the stale retry. A third request
        // must fall back without retaining another capture context.
        capture_lost = true;
        int first_lost = lost_count;
        assert(!prepare());
        usleep((useconds_t)(SPACE_SNAPSHOT_STALE_NS / 1000));
        assert(!prepare());
        usleep((useconds_t)(SPACE_SNAPSHOT_STALE_NS / 1000));
        int calls_at_bound = modern_capture_calls;
        assert(space_snapshot_capture_unresolved() == SPACE_SNAPSHOT_MAX_UNRESOLVED);
        assert(!prepare());
        assert(!capture(50)); // Navigation proceeds with no effect.
        assert(modern_capture_calls == calls_at_bound);
        assert(space_snapshot_capture_unresolved() == SPACE_SNAPSHOT_MAX_UNRESOLVED);

        // The newer callback returns first. Its slot is reusable; an older callback
        // must not clear the third generation's pending marker.
        deliver_lost(first_lost + 1);
        assert(!space_snapshot_capture_pending());
        assert(space_snapshot_capture_unresolved() == 1);
        assert(!prepare());
        assert(modern_capture_calls == calls_at_bound + 1);
        deliver_lost(first_lost);
        assert(space_snapshot_capture_pending());
        deliver_lost(first_lost + 2);
        assert(!space_snapshot_capture_pending());
        assert(space_snapshot_capture_unresolved() == 0);
        capture_lost = false;
        assert(prepare());
        space_navigation_snapshot_cancel();
        expect_released();

        // Concurrent completions return both slots exactly once.
        capture_lost = true;
        int parallel_lost = lost_count;
        assert(!prepare());
        usleep((useconds_t)(SPACE_SNAPSHOT_STALE_NS / 1000));
        assert(!prepare());
        assert(space_snapshot_capture_unresolved() == 2);
        dispatch_group_t group = dispatch_group_create();
        dispatch_group_async(group, dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{ deliver_lost(parallel_lost + 1); });
        dispatch_group_async(group, dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{ deliver_lost(parallel_lost); });
        dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
        dispatch_release(group);
        assert(space_snapshot_capture_unresolved() == 0);
        assert(!space_snapshot_capture_pending());
        capture_lost = false;
        assert(prepare());
        space_navigation_snapshot_cancel();
        expect_released();

        // Dropping a pending presentation does not cancel the framework request or
        // return its unresolved slot. Wrong/old tokens cannot discard a newer
        // request. No overlay is constructed by this path.
        capture_lost = true;
        int discard_lost = lost_count;
        assert(capture(70));
        struct space_snapshot_capture *discarded = space_snapshot_pending.capture;
        assert(discarded->references == 2 && space_snapshot_capture_unresolved() == 1);
        assert(space_navigation_snapshot_discard(69) == SPACE_SNAPSHOT_CANCELLED);
        assert(space_snapshot_pending.capture == discarded);
        assert(space_navigation_snapshot_discard(70) == SPACE_SNAPSHOT_MISSING);
        assert(!space_snapshot_pending.capture && discarded->references == 1);
        assert(space_snapshot_capture_unresolved() == 1 && space_snapshot_capture_pending());
        assert(space_navigation_snapshot_discard(70) == SPACE_SNAPSHOT_CANCELLED);
        assert(!capture(71)); // Discard is not admission-slot cancellation.
        usleep((useconds_t)(SPACE_SNAPSHOT_STALE_NS / 1000));
        assert(capture(71));
        assert(space_snapshot_capture_unresolved() == 2);
        struct space_snapshot_capture *newer = space_snapshot_pending.capture;
        assert(space_navigation_snapshot_discard(70) == SPACE_SNAPSHOT_CANCELLED);
        assert(space_snapshot_pending.capture == newer);
        assert(space_navigation_snapshot_discard(71) == SPACE_SNAPSHOT_MISSING);
        usleep((useconds_t)(SPACE_SNAPSHOT_STALE_NS / 1000));
        int discard_bound_calls = modern_capture_calls;
        assert(!capture(72));
        assert(modern_capture_calls == discard_bound_calls && space_snapshot_capture_unresolved() == 2);
        deliver_lost(discard_lost + 1); // Reverse callback order, after discard.
        assert(space_snapshot_capture_unresolved() == 1);
        deliver_lost(discard_lost);
        assert(space_snapshot_capture_unresolved() == 0 && !space_snapshot_capture_pending());
        assert(space_navigation_snapshot_present(70) == SPACE_SNAPSHOT_CANCELLED);
        assert(space_navigation_snapshot_present(71) == SPACE_SNAPSHOT_CANCELLED);
        assert(!live_windows && !live_spaces);
        capture_lost = false;
        usleep((useconds_t)(SPACE_SNAPSHOT_CAPTURE_NS / 1000)); // Drain old deadline reports.

        // Discard never tears down an already visible overlay.
        assert(prepare());
        assert(space_navigation_snapshot_discard(70) == SPACE_SNAPSHOT_CANCELLED);
        assert(live_windows == 1 && live_spaces == 1);
        space_navigation_snapshot_cancel();
        expect_released();

        // An asynchronous capture reports its callback, and presents its image; the
        // deadline reports too and finds nothing left.
        assert(capture(5));
        expect_reports(1);
        assert(__atomic_load_n(&captured_token, __ATOMIC_SEQ_CST) == 5);
        assert(space_navigation_snapshot_present(5) == SPACE_SNAPSHOT_READY);
        assert(space_navigation_snapshot_start(.03f, true));
        expect_released();
        expect_reports(2);
        assert(space_navigation_snapshot_present(5) == SPACE_SNAPSHOT_CANCELLED);

        // Only the token of the request waiting presents.
        assert(capture(6));
        expect_reports(1);
        assert(space_navigation_snapshot_present(7) == SPACE_SNAPSHOT_CANCELLED);
        assert(space_navigation_snapshot_present(6) == SPACE_SNAPSHOT_READY);
        space_navigation_snapshot_cancel();
        expect_released();
        expect_reports(2);

        // A late callback: the deadline reports first and the image is missing; the
        // callback reports later and releases its image.
        capture_delay = SPACE_SNAPSHOT_CAPTURE_NS * 2;
        assert(capture(8));
        expect_reports(1);
        assert(space_navigation_snapshot_present(8) == SPACE_SNAPSHOT_MISSING);
        expect_reports(2);
        while (space_snapshot_capture_pending())
            usleep(1000);
        capture_delay = 0;
        expect_released();

        // A capture that arrived in time stays usable however late the event loop
        // presents it.
        assert(capture(9));
        expect_reports(1);
        usleep((useconds_t) (SPACE_SNAPSHOT_CAPTURE_NS * 2 / 1000));
        assert(space_navigation_snapshot_present(9) == SPACE_SNAPSHOT_READY);
        space_navigation_snapshot_cancel();
        expect_released();
        expect_reports(2);

        // A request call that returns late is too late, like its callback.
        capture_start_delay = SPACE_SNAPSHOT_CAPTURE_NS * 2;
        assert(capture(10));
        capture_start_delay = 0;
        expect_reports(2);
        assert(space_navigation_snapshot_present(10) == SPACE_SNAPSHOT_MISSING);
        expect_released();

        // A denied capture has no image to present.
        deny_capture = true;
        assert(capture(11));
        expect_reports(1);
        deny_capture = false;
        assert(space_navigation_snapshot_present(11) == SPACE_SNAPSHOT_MISSING);
        expect_reports(2);

        // Cancelled while captured: presenting finds it cancelled, and the callback
        // still releases its image.
        assert(capture(12));
        space_navigation_snapshot_cancel();
        assert(space_navigation_snapshot_present(12) == SPACE_SNAPSHOT_CANCELLED);
        expect_reports(2);
        expect_released();

        // A synchronous preparation retires a capture still waited for.
        capture_delay = SPACE_SNAPSHOT_CAPTURE_NS / 2;
        assert(capture(13));
        while (space_snapshot_capture_pending())
            usleep(1000);
        capture_delay = 0;
        assert(prepare());
        assert(space_navigation_snapshot_present(13) == SPACE_SNAPSHOT_CANCELLED);
        space_navigation_snapshot_cancel();
        expect_released();
        expect_reports(2);

        expect_veil_retires_capture();

        // No capture starts while another is in flight: the step switches without
        // one and nothing reports.
        capture_delay = SPACE_SNAPSHOT_CAPTURE_NS * 2;
        assert(capture(14));
        expect_reports(1);
        assert(space_navigation_snapshot_present(14) == SPACE_SNAPSHOT_MISSING);
        assert(!capture(15));
        expect_reports(0);
        __atomic_store_n(&captured_reports, 1, __ATOMIC_SEQ_CST);
        expect_reports(2);
        while (space_snapshot_capture_pending())
            usleep(1000);
        capture_delay = 0;

        // The daemon recognises the auxiliary Spaces it created, and no other.
        assert(space_navigation_snapshot_owns_space(123));
        assert(!space_navigation_snapshot_owns_space(2) && !space_navigation_snapshot_owns_space(0));

        assert(modern_capture_calls > 0 && legacy_capture_calls == 0);
        CGImageRelease(wrong_image);
        CGImageRelease(fixture_image);
        puts("snapshot: alpha curves, veil, blurred veil, endpoint, cancellation, allocation failures, late and lost capture, asynchronous capture, colour space and watchdog passed");
    }
    return 0;
}
#pragma clang diagnostic pop
