// Opt-in live probe of an overlay window's first composited frame. It builds windows
// the way the daemon builds its veil, a black fill on an auxiliary Space of its own,
// in a process running AppKit as the daemon does, and orders them in over the main
// display in two ways, alternately:
//
//   at-alpha  ordered in at the overlay's alpha (the veil up to 8.0.1)
//   reveal    ordered in at alpha 0 and raised to it one refresh later
//
// A ScreenCaptureKit stream of the main display, 320 x 180, records the mean luma of
// every frame. A trial flashes when a frame within 150 ms of the order is brighter
// than the frame before it by more than 20: a black overlay can only darken the
// screen, so a brighter frame is the window shown with a white backing. The screen
// dims briefly once per trial; no Desktop changes.
//
// Build and run (needs Screen Recording for the process or its terminal):
//   xcrun clang -fobjc-arc -O2 tools/effects/first_frame_probe.m \
//     -F/System/Library/PrivateFrameworks -framework Cocoa -framework SkyLight \
//     -framework ScreenCaptureKit -framework CoreMedia -framework CoreVideo \
//     -o build/tools/first-frame-probe
//   build/tools/first-frame-probe [TRIALS] [ALPHA] [RESOLUTION]
//
// TRIALS per way (default 40), ALPHA the overlay's (default 0.4, the veil's) and
// RESOLUTION its backing pixels per point (default 1, the veil's; the crossfade's
// overlay uses the display's, 2 on Retina displays).
#import <Cocoa/Cocoa.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <CoreMedia/CoreMedia.h>
#include <pthread.h>
#include <time.h>

extern int SLSMainConnectionID(void);
extern CGError SLSNewWindowWithOpaqueShapeAndContext(int cid, int type, CFTypeRef region, CFTypeRef opaque_shape, int options, uint64_t *tags, float x, float y, int tag_size, uint32_t *wid, void *context);
extern CGError SLSReleaseWindow(int cid, uint32_t wid);
extern CGContextRef SLWindowContextCreate(int cid, uint32_t wid, CFDictionaryRef options);
extern CFTypeRef CGRegionCreateEmptyRegion(void);
extern CGError CGSNewRegionWithRect(CGRect *rect, CFTypeRef *region);
extern CGError SLSSetWindowResolution(int cid, uint32_t wid, double resolution);
extern CGError SLSSetWindowOpacity(int cid, uint32_t wid, bool opaque);
extern CGError SLSSetWindowLevel(int cid, uint32_t wid, int level);
extern CGError SLSSetWindowAlpha(int cid, uint32_t wid, float alpha);
extern CGError SLSOrderWindow(int cid, uint32_t wid, int mode, uint32_t rel_wid);
extern int SLSSpaceCreate(int cid, int type, int options);
extern CGError SLSSpaceDestroy(int cid, int sid);
extern void SLSSpaceSetAbsoluteLevel(int cid, int sid, int level);
extern void SLSShowSpaces(int cid, CFArrayRef spaces);
extern void SLSSpaceAddWindowsAndRemoveFromSpaces(int cid, int sid, CFArrayRef windows, int mask);

#define MAX_FRAMES 8192
#define MAX_TRIALS 512

static struct { uint64_t at; double luma; } frames[MAX_FRAMES];
static int frame_count;
static pthread_mutex_t frame_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t orders[2][MAX_TRIALS];
static int trials = 40;
static float alpha = 0.4f;
static double resolution = 1.0;

static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

@interface Recorder : NSObject <SCStreamOutput>
@end

@implementation Recorder
- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)buffer ofType:(SCStreamOutputType)type
{
    if (type != SCStreamOutputTypeScreen) return;
    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(buffer, false);
    if (!attachments || !CFArrayGetCount(attachments)) return;
    NSNumber *status = ((__bridge NSDictionary *) CFArrayGetValueAtIndex(attachments, 0))[SCStreamFrameInfoStatus];
    if (status.integerValue != SCFrameStatusComplete) return;
    CVPixelBufferRef pixels = CMSampleBufferGetImageBuffer(buffer);
    if (!pixels) return;

    uint64_t at = now_ns();
    CVPixelBufferLockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);
    size_t width = CVPixelBufferGetWidth(pixels), height = CVPixelBufferGetHeight(pixels);
    size_t pitch = CVPixelBufferGetBytesPerRow(pixels);
    const uint8_t *base = CVPixelBufferGetBaseAddress(pixels);
    double sum = 0.0;
    for (size_t y = 0; y < height; ++y) {
        const uint8_t *row = base + y * pitch;
        for (size_t x = 0; x < width; ++x) sum += 0.0722 * row[4 * x] + 0.7152 * row[4 * x + 1] + 0.2126 * row[4 * x + 2];
    }
    CVPixelBufferUnlockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);

    pthread_mutex_lock(&frame_lock);
    if (frame_count < MAX_FRAMES) frames[frame_count++] = (typeof(frames[0])) { at, sum / (width * height) };
    pthread_mutex_unlock(&frame_lock);
}
@end

static void show_overlay(int cid, CGRect bounds, bool reveal, uint64_t *ordered)
{
    CFTypeRef region = NULL, empty = CGRegionCreateEmptyRegion();
    CGSNewRegionWithRect(&bounds, &region);
    uint64_t tags = (1ULL << 1) | (1ULL << 9);
    uint32_t wid = 0;
    SLSNewWindowWithOpaqueShapeAndContext(cid, 2, region, empty, 13, &tags, 0, 0, 64, &wid, NULL);
    CFRelease(region);
    CFRelease(empty);
    if (!wid) return;

    SLSSetWindowResolution(cid, wid, resolution);
    SLSSetWindowOpacity(cid, wid, false);
    SLSSetWindowLevel(cid, wid, 1);
    CGContextRef context = SLWindowContextCreate(cid, wid, NULL);
    CGContextSetBlendMode(context, kCGBlendModeCopy);
    CGContextSetRGBFillColor(context, 0, 0, 0, 1);
    CGContextFillRect(context, CGRectMake(0, 0, bounds.size.width, bounds.size.height));
    CGContextFlush(context);
    SLSSetWindowAlpha(cid, wid, reveal ? 0.0f : alpha);

    int sid = SLSSpaceCreate(cid, 1, 0);
    CFNumberRef space = CFNumberCreate(NULL, kCFNumberIntType, &sid);
    CFNumberRef window = CFNumberCreate(NULL, kCFNumberSInt32Type, &wid);
    CFArrayRef spaces = CFArrayCreate(NULL, (const void **) &space, 1, &kCFTypeArrayCallBacks);
    CFArrayRef windows = CFArrayCreate(NULL, (const void **) &window, 1, &kCFTypeArrayCallBacks);
    SLSSpaceSetAbsoluteLevel(cid, sid, 0);
    SLSSpaceAddWindowsAndRemoveFromSpaces(cid, sid, windows, 7);
    SLSShowSpaces(cid, spaces);

    *ordered = now_ns();
    SLSOrderWindow(cid, wid, 1, 0);
    if (reveal) {
        usleep(16667);
        SLSSetWindowAlpha(cid, wid, alpha);
    }
    usleep(150000);

    if (context) CGContextRelease(context);
    SLSReleaseWindow(cid, wid);
    SLSSpaceDestroy(cid, sid);
    CFRelease(spaces);
    CFRelease(windows);
    CFRelease(space);
    CFRelease(window);
}

static int count_flashes(int way)
{
    int flashes = 0;
    for (int i = 0; i < trials; ++i) {
        uint64_t at = orders[way][i];
        double before = -1.0;
        bool flashed = false;
        for (int f = 0; f < frame_count; ++f) {
            if (frames[f].at < at) before = frames[f].luma;
            else if (frames[f].at < at + 150000000ULL && before >= 0.0 && frames[f].luma > before + 20.0) flashed = true;
        }
        flashes += flashed;
    }
    return flashes;
}

static void *run_trials(void *unused)
{
    (void) unused;
    int cid = SLSMainConnectionID();
    CGRect bounds = CGDisplayBounds(CGMainDisplayID());
    usleep(500000);
    for (int i = 0; i < trials; ++i) {
        for (int way = 0; way < 2; ++way) {
            @autoreleasepool { show_overlay(cid, bounds, way == 1, &orders[way][i]); }
            usleep(250000);
        }
    }
    usleep(300000);

    pthread_mutex_lock(&frame_lock);
    printf("%d frames recorded; alpha %.2f, resolution %.1f\n", frame_count, alpha, resolution);
    printf("at-alpha: %d of %d trials flashed\n", count_flashes(0), trials);
    printf("reveal:   %d of %d trials flashed\n", count_flashes(1), trials);
    pthread_mutex_unlock(&frame_lock);
    exit(frame_count ? 0 : 1);
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        if (argc > 1) trials = atoi(argv[1]);
        if (argc > 2) alpha = atof(argv[2]);
        if (argc > 3) resolution = atof(argv[3]);
        if (trials < 1 || trials > MAX_TRIALS || !(alpha > 0.0f && alpha <= 1.0f) || !(resolution >= 1.0 && resolution <= 3.0)) {
            fprintf(stderr, "usage: %s [TRIALS 1-%d] [ALPHA (0,1]] [RESOLUTION 1-3]\n", argv[0], MAX_TRIALS);
            return 64;
        }
        if (!CGPreflightScreenCaptureAccess()) {
            fprintf(stderr, "screen recording permission is required\n");
            return 1;
        }

        __block SCDisplay *display = nil;
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent *content, NSError *failure) {
            (void) failure;
            for (SCDisplay *candidate in content.displays) {
                if (candidate.displayID == CGMainDisplayID()) display = candidate;
            }
            dispatch_semaphore_signal(done);
        }];
        dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
        if (!display) {
            fprintf(stderr, "main display not shareable\n");
            return 1;
        }

        SCStreamConfiguration *config = [SCStreamConfiguration new];
        config.width = 320;
        config.height = 180;
        config.minimumFrameInterval = CMTimeMake(1, 120);
        config.queueDepth = 8;
        config.pixelFormat = 'BGRA';
        config.showsCursor = NO;
        Recorder *recorder = [Recorder new];
        SCStream *stream = [[SCStream alloc] initWithFilter:[[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]]
                                              configuration:config delegate:nil];
        NSError *error = nil;
        if (![stream addStreamOutput:recorder type:SCStreamOutputTypeScreen
                  sampleHandlerQueue:dispatch_queue_create("first-frame-probe", DISPATCH_QUEUE_SERIAL) error:&error]) {
            fprintf(stderr, "stream output: %s\n", error.description.UTF8String);
            return 1;
        }
        __block bool started = false;
        [stream startCaptureWithCompletionHandler:^(NSError *failure) {
            started = failure == nil;
            dispatch_semaphore_signal(done);
        }];
        dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
        if (!started) {
            fprintf(stderr, "stream did not start\n");
            return 1;
        }

        // The overlays are built off the main thread, as on the daemon's event loop,
        // while AppKit runs the main thread's run loop.
        [NSApplication sharedApplication];
        pthread_t thread;
        pthread_create(&thread, NULL, run_trials, NULL);
        [NSApp run];
    }
    return 0;
}
