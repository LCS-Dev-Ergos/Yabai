// Headless cost of the snapshot's two preparation phases at several output
// scales: the production ScreenCaptureKit request (snapshot_capture.m) and the
// production draw into an overlay window (snapshot_surface.m). The window is
// created exactly as the daemon creates it but never ordered in, so nothing
// appears on screen, no Desktop changes and focus is untouched.
//
// Scales run interleaved, one capture per scale per round, so drift in
// WindowServer load affects all of them alike. Each line is one sample:
//   scale width height capture_ms draw_ms
// capture_ms is request to callback; draw_ms covers the window context, the
// copy and the flush. A missing image prints capture_ms -1.
//
// usage: capture-scale-bench DISPLAY_ID ROUNDS SCALE...   (ROUNDS 1..20,
//        SCALE in (0, 1], fraction of the display's pixel width and height)
//
// Needs existing Screen Recording permission; never requests it.
#import <Cocoa/Cocoa.h>
#include <math.h>
#include <unistd.h>
#include "../../src/misc/extern.h"
#include "../../src/effects/display.m"

static uint64_t read_os_timer(void)
{
    return clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
}

static void space_navigation_snapshot_captured(int token)
{
    (void) token;
}

#include "../../src/effects/snapshot.h"

// The overlay reads its fade curve from the daemon's configuration.
static struct { int navigation_fade_curve; int navigation_veil_blur; } g_window_manager;

#include "../../src/effects/snapshot.m"

#define BENCH_MAX_SCALES 8

static double bench_scales[BENCH_MAX_SCALES];
static int bench_scale_count;

// A hidden overlay window with the daemon's flags and resolution, released
// by bench_window_destroy even when creation failed halfway.
static bool bench_window_create(struct space_snapshot *snapshot, CGRect bounds, size_t width)
{
    int cid = SLSMainConnectionID();
    CFTypeRef region = NULL;
    CFTypeRef empty = CGRegionCreateEmptyRegion();
    CGSNewRegionWithRect(&bounds, &region);
    uint64_t tags = (1ULL << 1) | (1ULL << 9);
    bool success = region && empty
        && SLSNewWindowWithOpaqueShapeAndContext(cid, 2, region, empty, 13, &tags, 0, 0, 64,
                                                &snapshot->window, NULL) == kCGErrorSuccess
        && snapshot->window
        && SLSSetWindowResolution(cid, snapshot->window, width / bounds.size.width) == kCGErrorSuccess
        && SLSSetWindowOpacity(cid, snapshot->window, false) == kCGErrorSuccess;
    if (region) CFRelease(region);
    if (empty) CFRelease(empty);
    return success;
}

static void bench_window_destroy(struct space_snapshot *snapshot)
{
    space_snapshot_surface_destroy(snapshot);
    if (snapshot->window) SLSReleaseWindow(SLSMainConnectionID(), snapshot->window);
    *snapshot = (struct space_snapshot) { 0 };
}

static void bench_sample(uint32_t display, double scale)
{
    CGRect bounds = CGDisplayBounds(display);
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display);
    if (!mode) return;
    size_t width = (size_t) lround(CGDisplayModeGetPixelWidth(mode) * scale);
    size_t height = (size_t) lround(CGDisplayModeGetPixelHeight(mode) * scale);
    CGDisplayModeRelease(mode);

    struct space_snapshot_capture *capture = space_snapshot_capture_start(bounds, width, height, 0);
    if (!capture) {
        printf("%.3f %zu %zu -1 -1\n", scale, width, height);
        return;
    }

    // Wait past the production deadline: the raw duration is the measurement.
    bool arrived = dispatch_semaphore_wait(capture->ready, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC)) == 0;
    double capture_ms = arrived && capture->image ? (capture->arrived - capture->began) / 1e6 : -1.0;
    CGImageRef image = arrived && capture->image ? CGImageRetain(capture->image) : NULL;
    space_snapshot_capture_release(capture);

    double draw_ms = -1.0;
    if (image) {
        struct space_snapshot snapshot = { 0 };
        if (bench_window_create(&snapshot, bounds, width)) {
            uint64_t drawn = read_os_timer();
            if (space_snapshot_surface_create(&snapshot, image, bounds)) draw_ms = (read_os_timer() - drawn) / 1e6;
        }
        bench_window_destroy(&snapshot);
        CGImageRelease(image);
    }

    printf("%.3f %zu %zu %.2f %.2f\n", scale, width, height, capture_ms, draw_ms);
    fflush(stdout);
}

int main(int argc, const char **argv)
{
    if (argc < 4 || argc - 3 > BENCH_MAX_SCALES) return 64;
    char *end = NULL;
    unsigned long display_value = strtoul(argv[1], &end, 10);
    if (end == argv[1] || *end || !display_value || display_value > UINT32_MAX) return 64;
    long rounds = strtol(argv[2], &end, 10);
    if (end == argv[2] || *end || rounds < 1 || rounds > 20) return 64;
    bench_scale_count = argc - 3;
    for (int i = 0; i < bench_scale_count; ++i) {
        bench_scales[i] = strtod(argv[3 + i], &end);
        if (end == argv[3 + i] || *end || !(bench_scales[i] > 0.0 && bench_scales[i] <= 1.0)) return 64;
    }
    if (!CGPreflightScreenCaptureAccess()) {
        fprintf(stderr, "Screen capture permission unavailable; no permission requested.\n");
        return 77;
    }
    if (@available(macOS 26.0, *)) { } else return 78;

    uint32_t display = (uint32_t) display_value;
    NSApplicationLoad(); // Match the daemon's AppKit/WindowServer setup.
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
        @autoreleasepool {
            printf("# scale width height capture_ms draw_ms\n");
            for (long round = 0; round < rounds; ++round) {
                for (int i = 0; i < bench_scale_count; ++i) {
                    int scale = (int) ((i + round) % bench_scale_count);
                    @autoreleasepool { bench_sample(display, bench_scales[scale]); }
                    usleep(400000);
                }
            }
            exit(0);
        }
    });
    CFRunLoopRun();
    return 0;
}
