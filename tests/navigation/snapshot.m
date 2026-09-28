// Exercise the actual asynchronous renderer with in-memory window/capture
// dependencies. No real Desktop is captured and no window is opened.
#import <Cocoa/Cocoa.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>

static uint64_t read_os_timer(void)
{
    return clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
}

static int live_windows, live_spaces, alpha_writes;
static bool deny_capture, fail_create, fail_context, fail_order, fail_attach, fail_alpha;
static uint64_t capture_delay, capture_start_delay;
static uint64_t active_sid = 2;
static bool overlay_attached;
static CGImageRef fixture_image;
static CGRect fixture_bounds = {{0, 0}, {16, 16}};

@interface SnapshotCaptureMock : NSObject
+ (void)captureImageInRect:(CGRect)rect completionHandler:(void (^)(CGImageRef, NSError *))handler;
@end
@implementation SnapshotCaptureMock
+ (void)captureImageInRect:(CGRect)rect completionHandler:(void (^)(CGImageRef, NSError *))handler
{
    (void)rect;
    bool denied = deny_capture;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, capture_delay), dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0), ^{
        handler(denied ? NULL : fixture_image, nil);
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
    *w = 77;
    __atomic_add_fetch(&live_windows, 1, __ATOMIC_SEQ_CST);
    return 0;
}

static CGError SLSSetWindowResolution(int c, uint32_t w, double s)
{
    (void)c;
    (void)w;
    (void)s;
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

static CGError SLSSetWindowAlpha(int c, uint32_t w, float alpha)
{
    (void)c;
    (void)w;
    assert(alpha >= 0 && alpha <= 1);
    __atomic_add_fetch(&alpha_writes, 1, __ATOMIC_SEQ_CST);
    return fail_alpha ? kCGErrorFailure : 0;
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
    return fail_order ? kCGErrorFailure : 0;
}

static CGContextRef bitmap(void)
{
    CGColorSpaceRef colors = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(NULL, 16, 16, 8, 64, colors, (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(colors);
    return ctx;
}

static CGContextRef SLWindowContextCreate(int c, uint32_t w, void *options)
{
    (void)c;
    (void)w;
    (void)options;
    return fail_context ? NULL : bitmap();
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

#define SCScreenshotManager SnapshotCaptureMock
#define CGPreflightScreenCaptureAccess fake_access
#define CGDisplayIsActive fake_active
#define CGDisplayBounds fake_bounds
#define CGDisplayCopyDisplayMode fake_mode
#define CGDisplayModeGetPixelWidth fake_pixels
#define CGDisplayModeGetPixelHeight fake_pixels
#define CGDisplayModeRelease fake_mode_release
#define CGDisplayCreateUUIDFromDisplayID fake_uuid
#include "../../src/space_navigation_snapshot.m"

static bool prepare(void)
{
    return space_navigation_snapshot_prepare(1, 2, 1.0f / 120.0f);
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

int main(void)
{
    @autoreleasepool
    {
        CGContextRef ctx = bitmap();
        fixture_image = CGBitmapContextCreateImage(ctx);
        CGContextRelease(ctx);
        assert(prepare());
        assert(space_navigation_snapshot_start(.03f, true));
        expect_released(); // Endpoint releases the overlay, with actual timer callbacks.
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
        capture_start_delay = SPACE_SNAPSHOT_CAPTURE_NS * 2;
        assert(!prepare()); // A callback signaled during a slow API call is still too late.
        capture_start_delay = 0;

        capture_delay = SPACE_SNAPSHOT_CAPTURE_NS * 2;
        assert(!prepare()); // Timeout, then a new request while the callback is late.
        assert(!prepare());
        while (__atomic_load_n(&space_snapshot_capturing, __ATOMIC_ACQUIRE))
            usleep(1000);
        capture_delay = 0;
        assert(prepare()); // The late callback did not resurrect its image/window.
        expect_released(); // Prepared but never started: watchdog releases it.
        CGImageRelease(fixture_image);
        puts("snapshot: endpoint, cancellation, allocation failures, late capture and watchdog passed");
    }
    return 0;
}
