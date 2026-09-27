// Records the frames WindowServer presents on one display, with ScreenCaptureKit.
// For every frame it prints the display time in CLOCK_UPTIME_RAW milliseconds,
// the frame status (0 complete, 1 idle: nothing changed), the mean luminance
// (0-255, Rec. 709) of the whole frame and of the central box, 30-70% of the
// width and 25-75% of the height, and the central box's mean red, green and
// blue, then luminance/RGB of the top 40 logical points (the bar region).
// Needs existing Screen Recording permission; never requests it. Optional
// PNG output is for visual diagnosis and adds work to the capture callback:
// do not use PNG-enabled captures to benchmark presentation timing.
//
// usage: frame-capture DISPLAY_ID SECONDS [EXISTING_PNG_DIRECTORY]
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include <mach/mach_time.h>
#import <ImageIO/ImageIO.h>
static NSString *output_directory;
static unsigned frame_number;

static double ticks_to_ms;

struct measure
{
    double luminance;
    double red, green, blue;
};

static struct measure measure(uint8_t *base, size_t stride, size_t x0, size_t y0, size_t x1, size_t y1)
{
    struct measure sum = { 0 };
    size_t count = 0;

    for (size_t y = y0; y < y1; y += 2) {
        uint8_t *row = base + y * stride;
        for (size_t x = x0; x < x1; x += 2) {
            uint8_t *pixel = row + x * 4;
            sum.blue += pixel[0];
            sum.green += pixel[1];
            sum.red += pixel[2];
            ++count;
        }
    }

    if (count) {
        sum.red /= count;
        sum.green /= count;
        sum.blue /= count;
        sum.luminance = 0.2126 * sum.red + 0.7152 * sum.green + 0.0722 * sum.blue;
    }

    return sum;
}

@interface Recorder : NSObject <SCStreamOutput, SCStreamDelegate>
@end

@implementation Recorder
- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)buffer ofType:(SCStreamOutputType)type
{
    if (type != SCStreamOutputTypeScreen) return;

    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(buffer, false);
    if (!attachments || !CFArrayGetCount(attachments)) return;

    NSDictionary *info = (__bridge NSDictionary *) CFArrayGetValueAtIndex(attachments, 0);
    NSInteger status = [info[SCStreamFrameInfoStatus] integerValue];
    double time = [info[SCStreamFrameInfoDisplayTime] unsignedLongLongValue] * ticks_to_ms;

    CVPixelBufferRef pixels = CMSampleBufferGetImageBuffer(buffer);
    if (status != SCFrameStatusComplete || !pixels) {
        printf("%.2f %ld\n", time, (long) status);
        return;
    }

    if (CVPixelBufferLockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) return;
    uint8_t *base = CVPixelBufferGetBaseAddress(pixels);
    size_t width = CVPixelBufferGetWidth(pixels);
    size_t height = CVPixelBufferGetHeight(pixels);
    size_t stride = CVPixelBufferGetBytesPerRow(pixels);
    if (!base || !width || !height) {
        CVPixelBufferUnlockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);
        return;
    }

    struct measure full = measure(base, stride, 0, 0, width, height);
    struct measure center = measure(base, stride, width * 3 / 10, height / 4, width * 7 / 10, height * 3 / 4);
    struct measure bar = measure(base, stride, 0, 0, width, MIN(height, 20));
    if (output_directory) {
        CGColorSpaceRef colors = CGColorSpaceCreateDeviceRGB();
        CGContextRef context = CGBitmapContextCreate(base, width, height, 8, stride, colors,
                                                     kCGBitmapByteOrder32Little | kCGImageAlphaNoneSkipFirst);
        CGImageRef image = context ? CGBitmapContextCreateImage(context) : NULL;
        NSString *path = [output_directory stringByAppendingPathComponent:
                          [NSString stringWithFormat:@"%04u-%.2f.png", frame_number++, time]];
        CGImageDestinationRef dest = CGImageDestinationCreateWithURL((__bridge CFURLRef)[NSURL fileURLWithPath:path],
                                                                    CFSTR("public.png"), 1, NULL);
        if (dest && image) {
            CGImageDestinationAddImage(dest, image, NULL);
            CGImageDestinationFinalize(dest);
        }
        if (dest) CFRelease(dest);
        if (image) CGImageRelease(image);
        if (context) CGContextRelease(context);
        CGColorSpaceRelease(colors);
    }
    CVPixelBufferUnlockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);

    printf("%.2f %ld %.2f %.2f %.1f %.1f %.1f %.2f %.1f %.1f %.1f\n", time, (long) status, full.luminance, center.luminance,
           center.red, center.green, center.blue, bar.luminance, bar.red, bar.green, bar.blue);
}

- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error
{
    fprintf(stderr, "capture stopped: %s\n", error.localizedDescription.UTF8String);
}
@end

int main(int argc, char **argv)
{
    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: %s DISPLAY_ID SECONDS [EXISTING_PNG_DIRECTORY]\n", argv[0]);
        return 64;
    }

    if (!CGPreflightScreenCaptureAccess()) {
        fprintf(stderr, "Screen Recording permission is unavailable\n");
        return 1;
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    mach_timebase_info_data_t timebase;
    mach_timebase_info(&timebase);
    ticks_to_ms = (double) timebase.numer / timebase.denom / 1e6;

    char *end = NULL;
    unsigned long display_value = strtoul(argv[1], &end, 10);
    if (end == argv[1] || *end || !display_value || display_value > UINT32_MAX) return 64;
    CGDirectDisplayID display_id = (CGDirectDisplayID) display_value;

    double seconds = strtod(argv[2], &end);
    if (end == argv[2] || *end || !(seconds >= 0.1 && seconds <= 30.0)) return 64;
    if (argc == 4) output_directory = [NSString stringWithUTF8String:argv[3]];

    @autoreleasepool {
        dispatch_semaphore_t ready = dispatch_semaphore_create(0);
        __block SCShareableContent *content = nil;
        __block NSError *failure = nil;

        [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent *result, NSError *error) {
            content = result;
            failure = error;
            dispatch_semaphore_signal(ready);
        }];
        if (dispatch_semaphore_wait(ready, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC))) {
            fprintf(stderr, "timed out enumerating displays\n");
            return 1;
        }

        if (!content) {
            fprintf(stderr, "no shareable content: %s\n", failure.localizedDescription.UTF8String);
            return 1;
        }

        SCDisplay *display = nil;
        for (SCDisplay *candidate in content.displays) {
            if (candidate.displayID == display_id) display = candidate;
        }

        if (!display) {
            fprintf(stderr, "display %u not found\n", display_id);
            return 1;
        }

        SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
        SCStreamConfiguration *configuration = [[SCStreamConfiguration alloc] init];
        configuration.width = display.width / 2;
        configuration.height = display.height / 2;
        configuration.pixelFormat = kCVPixelFormatType_32BGRA;
        configuration.minimumFrameInterval = CMTimeMake(1, 120);
        configuration.queueDepth = 8;
        configuration.showsCursor = NO;

        Recorder *recorder = [[Recorder alloc] init];
        SCStream *stream = [[SCStream alloc] initWithFilter:filter configuration:configuration delegate:recorder];
        dispatch_queue_t queue = dispatch_queue_create("frame-capture", DISPATCH_QUEUE_SERIAL);

        NSError *error = nil;
        if (![stream addStreamOutput:recorder type:SCStreamOutputTypeScreen sampleHandlerQueue:queue error:&error]) {
            fprintf(stderr, "cannot add output: %s\n", error.localizedDescription.UTF8String);
            return 1;
        }

        [stream startCaptureWithCompletionHandler:^(NSError *error) {
            failure = error;
            dispatch_semaphore_signal(ready);
        }];
        if (dispatch_semaphore_wait(ready, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC))) {
            fprintf(stderr, "timed out starting capture\n");
            return 1;
        }

        if (failure) {
            fprintf(stderr, "cannot start capture: %s\n", failure.localizedDescription.UTF8String);
            return 1;
        }

        fprintf(stderr, "capturing display %u at %ldx%ld\n", display_id, (long) configuration.width, (long) configuration.height);
        [NSThread sleepForTimeInterval:seconds];

        [stream stopCaptureWithCompletionHandler:^(NSError *stopped) {
            (void) stopped;
            dispatch_semaphore_signal(ready);
        }];
        if (dispatch_semaphore_wait(ready, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC))) {
            fprintf(stderr, "timed out stopping capture\n");
            return 1;
        }
    }

    return 0;
}
