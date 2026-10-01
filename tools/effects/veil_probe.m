// Opt-in live probe of a capture-free Desktop transition, without replacing
// the installed daemon or payload. A solid "veil" window appears at once on an
// auxiliary Space, the Desktop switches through the installed daemon's client,
// and the veil clears over the destination. Nothing is captured, so the
// probe needs no Screen Recording permission, and the first visible response
// is bounded by one window order-in instead of the ~70-100 ms capture floor.
//
// The probe changes the visible Desktop: run it only with an idle-guarded
// harness (check_bar.py style) that restores the original Desktop afterwards.
// Events go to stderr as "veil <event> <ms>", in milliseconds since process
// start on CLOCK_UPTIME_RAW; "veil start_uptime_ms" carries the absolute clock
// so a frame recorder on the same clock can be aligned.
//
// Usage:
//   veil-probe DISPLAY_ID DESTINATION_INDEX PEAK FADE_IN_MS FADE_OUT_MS
//              [--switch-at start|peak] [--switch-delay MS] [--color R,G,B]
//              [--blur RADIUS] [--tint ALPHA]
//
// --switch-delay (0..100, with --switch-at start) holds the switch until the
// veil has had that long to reach the screen: a new window on a new auxiliary
// Space can be presented after Dock has already shown the destination.
//
// --blur (0..100) asks WindowServer to blur what lies below the veil, on the
// GPU and live, so the switch underneath stays soft. --tint (0..1, default 1)
// is the opacity of the colour fill inside the window; with a blur, a light
// tint lets the blurred Desktop show through. PEAK then fades the whole
// window, blur included.
//   veil-probe --self-test
//
// Exit 0: veil shown, Desktop changed, veil cleared. 2: the switch failed, no
// Space change was seen within 1000 ms of the veil appearing, or the veil
// could not be set up (the veil is cleared first). 64: usage error.
//
// Build:
//   xcrun clang -fno-objc-arc -O2 -UNDEBUG -Wall -Wextra tools/effects/veil_probe.m \
//     -framework Cocoa -framework Carbon -framework CoreGraphics \
//     -framework ScreenCaptureKit -framework QuartzCore \
//     -F/System/Library/PrivateFrameworks -framework SkyLight \
//     -o build/tools/veil-probe
#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include "../../src/misc/extern.h"
#include "../../src/effects/display.m"
static uint64_t read_os_timer(void)
{
    return clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
}
// Nothing here waits for a capture's report; the probe never captures.
static void space_navigation_snapshot_captured(int token)
{
    (void) token;
}

#include "../../src/effects/snapshot.h"

// The overlay reads its fade curve from the daemon's configuration.
static struct { int navigation_fade_curve; int navigation_veil_blur; } g_window_manager;

#include "../../src/effects/snapshot.m"

// How long we wait for a Space change after the veil is shown before we give
// up, clear the veil and report failure.
#define VEIL_SWITCH_TIMEOUT_MS 1000.0
// How long we wait at exit for the switch client to report, so that its
// return code is logged and counted.
#define VEIL_RETURN_WAIT_NS (1000ULL * 1000000ULL)

// The veil's opacity ms_since_ready after it was ordered in. Fade-in is a
// quadratic ease-out from 0 to peak over fade_in_ms (immediately peak when
// that is 0); the veil then holds at peak until fade_out_begin_ms, which is
// negative while no fade-out was decided on. Fade-out is a quadratic
// ease-out of the veil's opacity over fade_out_ms: it changes fastest at the
// start so the clearing reads as immediate. Never outside [0, peak].
static float veil_alpha(double ms_since_ready, double fade_in_ms, double peak,
                        double fade_out_begin_ms_or_negative, double fade_out_ms)
{
    float top = (float) peak;
    if (!(ms_since_ready == ms_since_ready) || !(peak > 0.0)) return 0.0f;

    double alpha;
    if (fade_out_begin_ms_or_negative >= 0.0 && ms_since_ready >= fade_out_begin_ms_or_negative) {
        // Compare against the end time itself: (begin + length) - begin can
        // fall short of length in floating point.
        double u = (ms_since_ready - fade_out_begin_ms_or_negative) / fade_out_ms;
        double rest = 1.0 - u;
        bool over = !(fade_out_ms > 0.0) || ms_since_ready >= fade_out_begin_ms_or_negative + fade_out_ms;
        alpha = over ? 0.0 : peak * rest * rest;
    } else if (fade_in_ms > 0.0 && ms_since_ready < fade_in_ms) {
        double u = ms_since_ready > 0.0 ? ms_since_ready / fade_in_ms : 0.0;
        double rest = 1.0 - u;
        alpha = peak * (1.0 - rest * rest);
    } else {
        alpha = peak;
    }

    float result = (float) alpha;
    if (!(result > 0.0f)) return 0.0f;
    return result > top ? top : result;
}

static int veil_self_test(void)
{
    const double peaks[] = { 0.05, 0.4, 1.0 };
    const double fade_ins[] = { 0.0, 1.0, 40.0, 300.0 };
    const double fade_outs[] = { 1.0, 150.0, 600.0 };

    for (size_t p = 0; p < sizeof(peaks) / sizeof(*peaks); ++p) {
        for (size_t i = 0; i < sizeof(fade_ins) / sizeof(*fade_ins); ++i) {
            for (size_t o = 0; o < sizeof(fade_outs) / sizeof(*fade_outs); ++o) {
                double peak = peaks[p], in = fade_ins[i], out = fade_outs[o];
                float top = (float) peak;

                // Fade-in starts at 0, or at peak without one, and reaches
                // peak exactly at its end and while holding.
                float first = veil_alpha(0.0, in, peak, -1.0, out);
                assert(in > 0.0 ? first == 0.0f : first == top);
                assert(veil_alpha(in, in, peak, -1.0, out) == top);
                assert(veil_alpha(in + 1.0, in, peak, -1.0, out) == top);
                assert(veil_alpha(in + 5000.0, in, peak, -1.0, out) == top);

                float last = first;
                for (double ms = 0.0; ms <= in + 50.0; ms += 0.25) {
                    float alpha = veil_alpha(ms, in, peak, -1.0, out);
                    assert(alpha >= last);
                    assert(alpha >= 0.0f && alpha <= top);
                    last = alpha;
                }

                // Fade-out from a hold, and from the middle of a hold.
                double begins[] = { in, in + 1.0, in + 333.3 };
                for (size_t b = 0; b < sizeof(begins) / sizeof(*begins); ++b) {
                    double begin = begins[b];
                    assert(veil_alpha(begin, in, peak, begin, out) == top);
                    last = top;
                    for (double ms = begin; ms <= begin + out + 50.0; ms += 0.25) {
                        float alpha = veil_alpha(ms, in, peak, begin, out);
                        assert(alpha <= last);
                        assert(alpha >= 0.0f && alpha <= top);
                        last = alpha;
                    }
                    assert(veil_alpha(begin + out, in, peak, begin, out) == 0.0f);
                    assert(veil_alpha(begin + out + 1.0, in, peak, begin, out) == 0.0f);
                    assert(veil_alpha(begin + out + 1e6, in, peak, begin, out) == 0.0f);
                }
            }
        }
    }

    // The fade-out answers at once: for a 0.4 veil fading over 150 ms, 5% of
    // the change is reached before 5 ms into it.
    float top = (float) 0.4;
    assert(veil_alpha(5.0, 40.0, 0.4, 0.0, 150.0) <= top - 0.05f * top);
    assert(veil_alpha(40.0 + 5.0, 40.0, 0.4, 40.0, 150.0) <= top - 0.05f * top);

    // Degenerate input still stays inside the range.
    assert(veil_alpha(-10.0, 40.0, 0.4, -1.0, 150.0) == 0.0f);
    assert(veil_alpha(NAN, 40.0, 0.4, -1.0, 150.0) == 0.0f);
    assert(veil_alpha(INFINITY, 40.0, 0.4, -1.0, 150.0) == top);

    fprintf(stderr, "veil self-test passed\n");
    return 0;
}

// Written before the timer is resumed and read only afterwards, except for the
// atomics, which any thread may touch, and the fields marked as timer-only,
// which only the timer's queue touches once it runs. Dispatch resume orders
// the setup writes before the first tick.
static struct
{
    uint64_t start_ns;
    uint64_t ready_ns;
    uint32_t display;
    int destination;
    int cid;
    CGRect bounds;
    CFStringRef uuid;
    uint64_t start_space;
    double color[3];
    double peak;
    double fade_in_ms;
    double fade_out_ms;
    bool switch_at_peak;
    double switch_delay_ms;
    int blur_radius;
    double tint;
    struct space_snapshot overlay; // window, overlay Space and context
    dispatch_source_t timer;
    dispatch_semaphore_t returned_semaphore;

    atomic_bool launched;
    atomic_bool returned;
    atomic_int return_code;

    bool space_changed;            // timer-only
    double fade_out_begin_ms;      // timer-only, negative until decided
    bool finished;                 // timer-only
} veil = { .fade_out_begin_ms = -1.0 };

// One line per event, written with a single write so that lines from the
// timer queue and the switch queue never interleave.
static void veil_event(const char *event)
{
    char line[96];
    double ms = (clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - veil.start_ns) / 1e6;
    int length = snprintf(line, sizeof(line), "veil %s %.1f\n", event, ms);
    if (length > 0) (void) !write(STDERR_FILENO, line, (size_t) length);
}

// The switch client runs on its own queue, so the timer never waits for it.
static void veil_switch_launch(void)
{
    if (atomic_exchange(&veil.launched, true)) return;

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        @autoreleasepool {
            NSTask *task = [[NSTask alloc] init];
            task.executableURL = [NSURL fileURLWithPath:@"/run/current-system/sw/bin/yabai"];
            task.arguments = @[@"-m", @"space", @"--navigate", @"focus",
                               [NSString stringWithFormat:@"%d", veil.destination], @"1", @"0"];
            veil_event("switch_launch");
            NSError *error = nil;
            int rc = -1;
            if ([task launchAndReturnError:&error]) {
                [task waitUntilExit];
                rc = task.terminationStatus;
            }
            [task release];
            atomic_store(&veil.return_code, rc);
            atomic_store(&veil.returned, true);
            char event[48];
            snprintf(event, sizeof(event), "switch_returned rc=%d", rc);
            veil_event(event);
            dispatch_semaphore_signal(veil.returned_semaphore);
        }
    });
}

// Hides the window and releases everything the veil created. Safe on a
// partly built veil: each piece is released only if it exists.
static void veil_teardown(void)
{
    if (veil.overlay.window) {
        SLSSetWindowAlpha(veil.cid, veil.overlay.window, 0.0f);
        SLSOrderWindow(veil.cid, veil.overlay.window, 0, 0);
    }
    if (veil.overlay.backing) CGContextRelease(veil.overlay.backing);
    veil.overlay.backing = NULL;
    if (veil.overlay.window) SLSReleaseWindow(veil.cid, veil.overlay.window);
    veil.overlay.window = 0;
    if (veil.overlay.overlay_space) SLSSpaceDestroy(veil.cid, veil.overlay.overlay_space);
    veil.overlay.overlay_space = 0;
    if (veil.uuid) CFRelease(veil.uuid);
    veil.uuid = NULL;
}

// Timer queue, after the fade-out has ended.
static void veil_finish(void)
{
    veil.finished = true;
    dispatch_source_cancel(veil.timer);
    dispatch_release(veil.timer);
    veil.timer = NULL;

    veil_teardown();

    // The switch client normally returned long ago; if not, give it a moment
    // so its code is counted rather than lost.
    if (atomic_load(&veil.launched) && !atomic_load(&veil.returned)) {
        dispatch_semaphore_wait(veil.returned_semaphore,
                                dispatch_time(DISPATCH_TIME_NOW, (int64_t) VEIL_RETURN_WAIT_NS));
    }
    bool switch_failed = atomic_load(&veil.returned) && atomic_load(&veil.return_code) != 0;
    veil_event("done");
    exit(veil.space_changed && !switch_failed ? 0 : 2);
}

static void veil_tick(void)
{
    if (veil.finished) return;

    double ms = (clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - veil.ready_ns) / 1e6;
    bool fade_in_done = ms >= veil.fade_in_ms;

    if (veil.switch_at_peak ? fade_in_done : ms >= veil.switch_delay_ms) veil_switch_launch();

    // Only a change after the switch was launched counts as ours.
    if (!veil.space_changed && atomic_load(&veil.launched)) {
        uint64_t space = SLSManagedDisplayGetCurrentSpace(veil.cid, veil.uuid);
        if (space && space != veil.start_space) {
            veil.space_changed = true;
            veil_event("space_changed");
        }
    }

    // The fade-out starts from a full hold, never from the middle of the
    // fade-in, so the veil's opacity is continuous. A failed switch or the
    // timeout ends the hold as well.
    if (veil.fade_out_begin_ms < 0.0 && fade_in_done) {
        bool switch_failed = atomic_load(&veil.returned) && atomic_load(&veil.return_code) != 0;
        if (veil.space_changed || switch_failed || ms >= VEIL_SWITCH_TIMEOUT_MS) {
            veil.fade_out_begin_ms = ms;
            veil_event("fade_out_begin");
        }
    }

    if (veil.fade_out_begin_ms >= 0.0 && ms >= veil.fade_out_begin_ms + veil.fade_out_ms) {
        veil_finish();
        return;
    }

    float alpha = veil_alpha(ms, veil.fade_in_ms, veil.peak, veil.fade_out_begin_ms, veil.fade_out_ms);
    SLSSetWindowAlpha(veil.cid, veil.overlay.window, alpha);
}

static bool veil_parse_uint(const char *text, unsigned long maximum, unsigned long *value)
{
    if (!text || *text < '0' || *text > '9') return false;
    char *end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(text, &end, 10);
    if (errno || end == text || *end || parsed > maximum) return false;
    *value = parsed;
    return true;
}

static bool veil_parse_color(const char *text, unsigned long rgb[3])
{
    const char *cursor = text;
    for (int i = 0; i < 3; ++i) {
        if (!cursor || *cursor < '0' || *cursor > '9') return false;
        char *end = NULL;
        errno = 0;
        unsigned long parsed = strtoul(cursor, &end, 10);
        if (errno || end == cursor || parsed > 255) return false;
        rgb[i] = parsed;
        if (i < 2) {
            if (*end != ',') return false;
            cursor = end + 1;
        } else if (*end) {
            return false;
        }
    }
    return true;
}

static int veil_usage(void)
{
    fprintf(stderr,
            "usage: veil-probe DISPLAY_ID DESTINATION_INDEX PEAK FADE_IN_MS FADE_OUT_MS"
            " [--switch-at start|peak] [--switch-delay MS] [--color R,G,B]"
            " [--blur RADIUS] [--tint ALPHA]\n"
            "       veil-probe --self-test\n");
    return 64;
}

// Builds the window, its fill and its auxiliary Space, with the window still
// fully transparent or at its first alpha. False leaves a partial veil for
// veil_teardown.
static bool veil_build(void)
{
    int cid = veil.cid;
    CGRect bounds = veil.bounds;

    CFTypeRef region = NULL;
    CFTypeRef empty = CGRegionCreateEmptyRegion();
    CGSNewRegionWithRect(&bounds, &region);
    // No activation or mouse events. An auxiliary Space, rather than the
    // sticky tag, keeps the veil visible while Dock hides the source Space.
    uint64_t tags = (1ULL << 1) | (1ULL << 9);
    bool success = region && empty
        && SLSNewWindowWithOpaqueShapeAndContext(cid, 2, region, empty, 13, &tags, 0, 0, 64,
                                                &veil.overlay.window, NULL) == kCGErrorSuccess
        && veil.overlay.window;
    if (region) CFRelease(region);
    if (empty) CFRelease(empty);
    if (!success) return false;

    // A solid colour needs no Retina backing: scale 1 fills a quarter of the
    // pixels.
    if (SLSSetWindowResolution(cid, veil.overlay.window, 1.0) != kCGErrorSuccess
        || SLSSetWindowOpacity(cid, veil.overlay.window, false) != kCGErrorSuccess
        || SLSSetWindowLevel(cid, veil.overlay.window, 1) != kCGErrorSuccess) return false;

    // Draw before ordering: a remote layer can present uninitialized backing.
    veil.overlay.backing = SLWindowContextCreate(cid, veil.overlay.window, NULL);
    if (!veil.overlay.backing) return false;
    CGContextSetBlendMode(veil.overlay.backing, kCGBlendModeCopy);
    CGContextSetRGBFillColor(veil.overlay.backing, veil.color[0], veil.color[1], veil.color[2], veil.tint);
    CGContextFillRect(veil.overlay.backing, CGRectMake(0, 0, bounds.size.width, bounds.size.height));
    CGContextFlush(veil.overlay.backing);

    // The blur samples whatever WindowServer composites below the window, so
    // it follows the switch underneath without any work of ours per frame.
    if (veil.blur_radius > 0
        && SLSSetWindowBackgroundBlurRadiusStyle(cid, veil.overlay.window, veil.blur_radius, 1) != kCGErrorSuccess) {
        return false;
    }

    // The first alpha is set before the window is ordered in, so the veil
    // never flashes at the wrong opacity.
    float first = veil_alpha(0.0, veil.fade_in_ms, veil.peak, -1.0, veil.fade_out_ms);
    if (SLSSetWindowAlpha(cid, veil.overlay.window, first) != kCGErrorSuccess) return false;

    // The helper records the Space under the lock that guards the daemon's
    // list of recent auxiliary Spaces.
    pthread_mutex_lock(&space_snapshot_lock);
    bool spaced = space_snapshot_space_create(&veil.overlay);
    pthread_mutex_unlock(&space_snapshot_lock);
    if (!spaced) return false;

    return SLSOrderWindow(cid, veil.overlay.window, 1, 0) == kCGErrorSuccess;
}

int main(int argc, const char **argv)
{
    veil.start_ns = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    atomic_init(&veil.launched, false);
    atomic_init(&veil.returned, false);
    atomic_init(&veil.return_code, 0);

    if (argc == 2 && strcmp(argv[1], "--self-test") == 0) return veil_self_test();
    if (argc < 6) return veil_usage();

    unsigned long display_value = 0, destination = 0, fade_in = 0, fade_out = 0;
    if (!veil_parse_uint(argv[1], UINT32_MAX, &display_value) || !display_value) return veil_usage();
    if (!veil_parse_uint(argv[2], 65535, &destination) || !destination) return veil_usage();

    if (*argv[3] != '.' && (*argv[3] < '0' || *argv[3] > '9')) return veil_usage();
    char *end = NULL;
    double peak = strtod(argv[3], &end);
    if (end == argv[3] || *end || !isfinite(peak) || !(peak > 0.0 && peak <= 1.0)) return veil_usage();

    if (!veil_parse_uint(argv[4], 300, &fade_in)) return veil_usage();
    if (!veil_parse_uint(argv[5], 600, &fade_out) || !fade_out) return veil_usage();

    bool switch_at_peak = false, seen_switch_at = false, seen_color = false, seen_delay = false;
    bool seen_blur = false, seen_tint = false;
    unsigned long switch_delay = 0, blur = 0;
    double tint = 1.0;
    unsigned long rgb[3] = { 0, 0, 0 };
    for (int i = 6; i < argc; i += 2) {
        if (i + 1 >= argc) return veil_usage();
        if (strcmp(argv[i], "--switch-at") == 0 && !seen_switch_at) {
            seen_switch_at = true;
            if (strcmp(argv[i + 1], "start") == 0) switch_at_peak = false;
            else if (strcmp(argv[i + 1], "peak") == 0) switch_at_peak = true;
            else return veil_usage();
        } else if (strcmp(argv[i], "--switch-delay") == 0 && !seen_delay) {
            seen_delay = true;
            if (!veil_parse_uint(argv[i + 1], 100, &switch_delay)) return veil_usage();
        } else if (strcmp(argv[i], "--blur") == 0 && !seen_blur) {
            seen_blur = true;
            if (!veil_parse_uint(argv[i + 1], 100, &blur)) return veil_usage();
        } else if (strcmp(argv[i], "--tint") == 0 && !seen_tint) {
            seen_tint = true;
            char *tint_end = NULL;
            tint = strtod(argv[i + 1], &tint_end);
            if (tint_end == argv[i + 1] || *tint_end || !isfinite(tint) || tint < 0.0 || tint > 1.0) return veil_usage();
        } else if (strcmp(argv[i], "--color") == 0 && !seen_color) {
            seen_color = true;
            if (!veil_parse_color(argv[i + 1], rgb)) return veil_usage();
        } else {
            return veil_usage();
        }
    }

    veil.display = (uint32_t) display_value;
    veil.destination = (int) destination;
    for (int i = 0; i < 3; ++i) veil.color[i] = rgb[i] / 255.0;
    veil.peak = peak;
    veil.fade_in_ms = (double) fade_in;
    veil.fade_out_ms = (double) fade_out;
    veil.switch_at_peak = switch_at_peak;
    veil.switch_delay_ms = (double) switch_delay;
    veil.blur_radius = (int) blur;
    veil.tint = tint;
    veil.returned_semaphore = dispatch_semaphore_create(0);
    if (!veil.returned_semaphore) return 2;

    fprintf(stderr, "veil start_uptime_ms %.3f\n", veil.start_ns / 1e6);

    NSApplicationLoad(); // Match the daemon's AppKit/WindowServer setup.
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
        @autoreleasepool {
            // Process launch and AppKit setup are paid once by the daemon, so
            // the harness measures the transition from here.
            fprintf(stderr, "veil work_begin_uptime_ms %.3f\n", clock_gettime_nsec_np(CLOCK_UPTIME_RAW) / 1e6);
            veil.cid = SLSMainConnectionID();
            veil.bounds = CGDisplayBounds(veil.display);
            if (!(veil.bounds.size.width > 0.0 && veil.bounds.size.height > 0.0)) {
                fprintf(stderr, "veil error unknown display %u\n", veil.display);
                exit(64);
            }
            CFUUIDRef uuid = CGDisplayCreateUUIDFromDisplayID(veil.display);
            if (!uuid) {
                fprintf(stderr, "veil error no display UUID for %u\n", veil.display);
                exit(64);
            }
            veil.uuid = CFUUIDCreateString(NULL, uuid);
            CFRelease(uuid);
            veil.start_space = veil.uuid ? SLSManagedDisplayGetCurrentSpace(veil.cid, veil.uuid) : 0;
            float interval = space_navigation_frame_interval(veil.display);
            if (!veil.start_space || !(interval > 0.0f)) {
                fprintf(stderr, "veil error no current Space\n");
                veil_teardown();
                exit(2);
            }

            if (!veil_build()) {
                fprintf(stderr, "veil error overlay setup failed\n");
                veil_teardown();
                exit(2);
            }

            veil.timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0,
                                                dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0));
            if (!veil.timer) {
                fprintf(stderr, "veil error timer setup failed\n");
                veil_teardown();
                exit(2);
            }
            dispatch_source_set_event_handler(veil.timer, ^{ @autoreleasepool { veil_tick(); } });
            dispatch_source_set_timer(veil.timer, DISPATCH_TIME_NOW, (uint64_t) (interval * 1e9), 1000000ULL);

            // Everything the timer reads is written before this point.
            veil.ready_ns = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
            veil_event("ready");
            if (!veil.switch_at_peak && veil.switch_delay_ms <= 0.0) veil_switch_launch();
            dispatch_resume(veil.timer);
        }
    });
    CFRunLoopRun();
    return 0;
}
