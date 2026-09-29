// Isolated, on-demand ScreenCaptureKit/Metal feasibility probe. It does not
// create a window, switch Spaces, or install a daemon. Screen contents are
// saved only with the explicit optional PNG prefix.
// Build/run instructions and the acceptance boundary are in the companion report.
#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <Metal/Metal.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreGraphics/CoreGraphics.h>
#import <IOSurface/IOSurface.h>
#import <ImageIO/ImageIO.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <libproc.h>
#include <sys/resource.h>
#include <errno.h>

static uint64_t clock_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
static double elapsed_ms(uint64_t begin) { return (clock_ns() - begin) / 1e6; }

typedef enum { PATH_MIXED, PATH_RECT, PATH_FILTER } CapturePath;

static pid_t windowserver_pid(void)
{
    // A one-time ps lookup works even when libproc enumeration hides other
    // users' processes. PID identity is recorded in every sample.
    FILE *listing = popen("/bin/ps -A -o pid=,comm=", "r");
    if (!listing) return 0;
    pid_t found = 0;
    char line[2048];
    while (fgets(line, sizeof(line), listing)) {
        int candidate = 0;
        char path[1024] = {0};
        if (sscanf(line, "%d %1023s", &candidate, path) != 2) continue;
        const char *name = strrchr(path, '/');
        if (!strcmp(name ? name + 1 : path, "WindowServer")) {
            found = candidate;
            break;
        }
    }
    pclose(listing);
    return found;
}

static double windowserver_top_mib(pid_t ws_pid)
{
    if (!ws_pid) return -1;
    char command[128];
    snprintf(command, sizeof(command), "/usr/bin/top -l 1 -pid %d -stats pid,command,mem", ws_pid);
    FILE *sample = popen(command, "r");
    if (!sample) return -1;
    double mib = -1;
    char line[2048];
    while (fgets(line, sizeof(line), sample)) {
        int pid = 0;
        char memory[32] = {0};
        if (sscanf(line, "%d %*s %31s", &pid, memory) != 2 || pid != ws_pid) continue;
        char *end = NULL;
        double number = strtod(memory, &end);
        if (end && end != memory) {
            if (*end == 'G') mib = number * 1024;
            else if (*end == 'M') mib = number;
            else if (*end == 'K') mib = number / 1024;
        }
    }
    int status = pclose(sample);
    return status == 0 ? mib : -1;
}

static void sample_memory(const char *point, uint64_t hold_begin, pid_t ws_pid, bool sample_ws_top)
{
    uint64_t started = clock_ns();
    double hold_at_start_ms = hold_begin ? (started - hold_begin) / 1e6 : 0.0;
    struct rusage_info_v0 client = {0}, ws = {0};
    int client_rc = proc_pid_rusage(getpid(), RUSAGE_INFO_V0, (rusage_info_t *)&client);
    int client_error = client_rc ? errno : 0;
    int ws_rc = ws_pid ? proc_pid_rusage(ws_pid, RUSAGE_INFO_V0, (rusage_info_t *)&ws) : -1;
    int ws_error = ws_rc ? (ws_pid ? errno : ESRCH) : 0;
    double top_mib = sample_ws_top ? windowserver_top_mib(ws_pid) : -1;
    printf("{\"mode\":\"memory\",\"point\":\"%s\",\"pid\":%d,\"windowserver_pid\":%d,\"hold_elapsed_ms\":%.3f,\"sample_duration_ms\":%.3f,\"client_footprint_mib\":%.3f,\"client_resident_mib\":%.3f,\"windowserver_footprint_mib\":%.3f,\"windowserver_resident_mib\":%.3f,\"windowserver_top_mem_mib\":%.3f,\"client_errno\":%d,\"windowserver_errno\":%d}\n",
           point, getpid(), ws_pid, hold_at_start_ms, elapsed_ms(started),
           client_rc ? -1.0 : client.ri_phys_footprint / 1048576.0,
           client_rc ? -1.0 : client.ri_resident_size / 1048576.0,
           ws_rc ? -1.0 : ws.ri_phys_footprint / 1048576.0,
           ws_rc ? -1.0 : ws.ri_resident_size / 1048576.0,
           top_mib,
           client_error, ws_error);
    fflush(stdout);
}

static void hold_with_samples(int seconds, pid_t ws_pid)
{
    if (!seconds) return;
    uint64_t begin = clock_ns();
    printf("{\"mode\":\"hold_begin\",\"seconds\":%d,\"pid\":%d}\n", seconds, getpid());
    fflush(stdout);
    sample_memory("hold_0", begin, ws_pid, true);
    const int checkpoints[] = {15, 30, 60, 120};
    for (size_t i = 0; i < sizeof(checkpoints) / sizeof(checkpoints[0]); ++i) {
        if (checkpoints[i] > seconds) break;
        uint64_t deadline = begin + (uint64_t)checkpoints[i] * NSEC_PER_SEC;
        while (clock_ns() < deadline) {
            uint64_t remaining = deadline - clock_ns();
            usleep((useconds_t)(remaining > 100000000ULL ? 100000 : remaining / 1000));
        }
        char point[24];
        snprintf(point, sizeof(point), "hold_%d", checkpoints[i]);
        sample_memory(point, begin, ws_pid, true);
    }
    if (seconds != 15 && seconds != 30 && seconds != 60 && seconds != 120) {
        uint64_t deadline = begin + (uint64_t)seconds * NSEC_PER_SEC;
        while (clock_ns() < deadline) {
            uint64_t remaining = deadline - clock_ns();
            usleep((useconds_t)(remaining > 100000000ULL ? 100000 : remaining / 1000));
        }
        sample_memory("hold_end", begin, ws_pid, true);
    }
    printf("{\"mode\":\"hold_end\",\"elapsed_ms\":%.3f,\"pid\":%d}\n", elapsed_ms(begin), getpid());
    fflush(stdout);
}

static bool color_details(CGColorSpaceRef color, char output[96], size_t *icc_bytes,
                          unsigned long long *icc_hash)
{
    *icc_bytes = 0;
    *icc_hash = 0;
    snprintf(output, 96, "unknown");
    if (!color) return false;
    CFStringRef name = CGColorSpaceGetName(color);
    if (name) CFStringGetCString(name, output, 96, kCFStringEncodingUTF8);
    for (char *p = output; *p; ++p) if (*p == '"' || *p == '\\') *p = '_';
    CFDataRef profile = CGColorSpaceCopyICCData(color);
    if (profile) {
        *icc_bytes = (size_t)CFDataGetLength(profile);
        const UInt8 *bytes = CFDataGetBytePtr(profile);
        unsigned long long hash = 1469598103934665603ULL;
        for (size_t i = 0; i < *icc_bytes; ++i) hash = (hash ^ bytes[i]) * 1099511628211ULL;
        *icc_hash = hash;
        CFRelease(profile);
    }
    return true;
}

static void timeout_exit(const char *phase)
{
    // A framework callback is not cancelled by a timeout. This one-shot tool
    // exits instead of issuing another request with an unresolved callback.
    fprintf(stderr, "timeout in %s; no further captures\n", phase);
    exit(2);
}

static SCDisplay *find_display(CGDirectDisplayID display_id, double *discovery_ms)
{
    __block SCShareableContent *content = nil;
    __block NSError *failure = nil;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    uint64_t begin = clock_ns();
    [SCShareableContent getShareableContentWithCompletionHandler:^(SCShareableContent *result, NSError *error) {
        content = result;
        failure = error;
        dispatch_semaphore_signal(done);
    }];
    if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC))) timeout_exit("display discovery");
    *discovery_ms = elapsed_ms(begin);
    if (failure || !content) {
        fprintf(stderr, "shareable content unavailable: %ld\n", (long)failure.code);
        return nil;
    }
    for (SCDisplay *display in content.displays) if (display.displayID == display_id) return display;
    fprintf(stderr, "display %u is absent from ScreenCaptureKit content\n", display_id);
    return nil;
}

static CGImageRef capture_rectangle(CGRect bounds, size_t width, size_t height, double *capture_ms)
{
    SCScreenshotConfiguration *config = [SCScreenshotConfiguration new];
    config.width = (NSInteger)width;
    config.height = (NSInteger)height;
    config.showsCursor = YES;
    config.dynamicRange = SCScreenshotDynamicRangeSDR;
    config.displayIntent = SCScreenshotDisplayIntentLocal;
    __block CGImageRef owned = NULL;
    __block NSInteger error_code = 0;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    uint64_t begin = clock_ns();
    [SCScreenshotManager captureScreenshotWithRect:bounds configuration:config
        completionHandler:^(SCScreenshotOutput *output, NSError *error) {
            CGImageRef image = output.sdrImage;
            if (image) owned = CGImageRetain(image);
            error_code = error.code;
            dispatch_semaphore_signal(done);
        }];
    if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC))) timeout_exit("rectangle capture");
    *capture_ms = elapsed_ms(begin);
    if (!owned) fprintf(stderr, "rectangle capture failed: %ld\n", (long)error_code);
    // Keep the configuration live until the asynchronous callback has finished.
    (void)config.width;
    return owned;
}

static CMSampleBufferRef capture_filtered(SCContentFilter *filter, SCStreamConfiguration *config,
                                          double *capture_ms)
{
    __block CMSampleBufferRef owned = NULL;
    __block NSInteger error_code = 0;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    uint64_t begin = clock_ns();
    [SCScreenshotManager captureSampleBufferWithFilter:filter configuration:config
        completionHandler:^(CMSampleBufferRef sample, NSError *error) {
            if (sample) owned = (CMSampleBufferRef)CFRetain(sample);
            error_code = error.code;
            dispatch_semaphore_signal(done);
        }];
    if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC))) timeout_exit("filtered buffer capture");
    *capture_ms = elapsed_ms(begin);
    if (!owned) fprintf(stderr, "filtered buffer capture failed: %ld\n", (long)error_code);
    (void)config.width;
    return owned;
}

static bool draw_cgimage(CGImageRef image, size_t width, size_t height, double *draw_ms)
{
    if (!image || CGImageGetWidth(image) != width || CGImageGetHeight(image) != height) return false;
    size_t row = width * 4;
    CGColorSpaceRef color = CGImageGetColorSpace(image);
    if (!color) return false;
    CGContextRef context = CGBitmapContextCreate(NULL, width, height, 8, row, color,
        kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    if (!context) return false;
    uint64_t begin = clock_ns();
    CGContextSetInterpolationQuality(context, kCGInterpolationNone);
    CGContextSetBlendMode(context, kCGBlendModeCopy);
    CGContextDrawImage(context, CGRectMake(0, 0, width, height), image);
    CGContextFlush(context);
    *draw_ms = elapsed_ms(begin);
    CGContextRelease(context);
    return true;
}

static bool save_png(CGImageRef image, NSString *path)
{
    NSURL *url = [NSURL fileURLWithPath:path];
    CGImageDestinationRef destination = CGImageDestinationCreateWithURL((__bridge CFURLRef)url,
        CFSTR("public.png"), 1, NULL);
    if (!destination) return false;
    CGImageDestinationAddImage(destination, image, NULL);
    bool saved = CGImageDestinationFinalize(destination);
    CFRelease(destination);
    return saved;
}

static bool save_buffer_png(CVPixelBufferRef pixels, NSString *path)
{
    if (!pixels || CVPixelBufferGetPixelFormatType(pixels) != kCVPixelFormatType_32BGRA ||
        CVPixelBufferLockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) return false;
    size_t width = CVPixelBufferGetWidth(pixels), height = CVPixelBufferGetHeight(pixels);
    size_t row = CVPixelBufferGetBytesPerRow(pixels);
    CGColorSpaceRef color = CVImageBufferGetColorSpace(pixels);
    bool owned_color = color == NULL;
    if (!color) color = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, CVPixelBufferGetBaseAddress(pixels), row * height, NULL);
    CGImageRef image = provider ? CGImageCreate(width, height, 8, 32, row, color,
        kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little,
        provider, NULL, false, kCGRenderingIntentDefault) : NULL;
    bool saved = image && save_png(image, path);
    if (image) CGImageRelease(image);
    if (provider) CGDataProviderRelease(provider);
    if (owned_color) CGColorSpaceRelease(color);
    CVPixelBufferUnlockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);
    return saved;
}

// The sample buffer remains retained by the caller until GPU completion.
// A single private target texture exists per request and is released on return.
static bool copy_buffer_on_gpu(CVPixelBufferRef pixels, id<MTLDevice> device,
                               id<MTLCommandQueue> queue, CVMetalTextureCacheRef cache,
                               size_t width, size_t height, bool verify,
                               double *encode_ms, double *complete_ms, double *gpu_ms)
{
    if (!pixels || CVPixelBufferGetWidth(pixels) != width || CVPixelBufferGetHeight(pixels) != height ||
        CVPixelBufferGetPixelFormatType(pixels) != kCVPixelFormatType_32BGRA ||
        !CVPixelBufferGetIOSurface(pixels)) return false;

    CVMetalTextureRef wrapped = NULL;
    uint64_t begin = clock_ns();
    CVReturn status = CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, cache, pixels, NULL,
        MTLPixelFormatBGRA8Unorm, width, height, 0, &wrapped);
    if (status != kCVReturnSuccess || !wrapped) return false;
    id<MTLTexture> source = CVMetalTextureGetTexture(wrapped);
    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
        width:width height:height mipmapped:NO];
    descriptor.storageMode = MTLStorageModePrivate;
    descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
    id<MTLTexture> target = [device newTextureWithDescriptor:descriptor];
    id<MTLCommandBuffer> command = [queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
    size_t readback_row = ((width * 4 + 255) / 256) * 256;
    id<MTLBuffer> readback = verify ? [device newBufferWithLength:readback_row * height
        options:MTLResourceStorageModeShared] : nil;
    if (!source || !target || !command || !blit || (verify && !readback)) {
        if (blit) [blit endEncoding];
        CFRelease(wrapped);
        return false;
    }
    MTLOrigin zero = {0, 0, 0};
    MTLSize size = {width, height, 1};
    [blit copyFromTexture:source sourceSlice:0 sourceLevel:0 sourceOrigin:zero sourceSize:size
               toTexture:target destinationSlice:0 destinationLevel:0 destinationOrigin:zero];
    if (verify) {
        [blit copyFromTexture:target sourceSlice:0 sourceLevel:0 sourceOrigin:zero sourceSize:size
                     toBuffer:readback destinationOffset:0 destinationBytesPerRow:readback_row
          destinationBytesPerImage:readback_row * height];
    }
    [blit endEncoding];
    *encode_ms = elapsed_ms(begin);
    dispatch_semaphore_t completed = dispatch_semaphore_create(0);
    [command addCompletedHandler:^(id<MTLCommandBuffer> finished) {
        (void)finished;
        dispatch_semaphore_signal(completed);
    }];
    [command commit];
    if (dispatch_semaphore_wait(completed, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC))) timeout_exit("GPU completion");
    *complete_ms = elapsed_ms(begin);
    *gpu_ms = command.GPUStartTime > 0 && command.GPUEndTime >= command.GPUStartTime
        ? (command.GPUEndTime - command.GPUStartTime) * 1000.0 : 0;
    bool passed = command.status == MTLCommandBufferStatusCompleted;
    if (verify && passed) {
        uint8_t *bytes = readback.contents;
        for (size_t y = 0; y < height && passed; ++y) {
            for (size_t x = 0; x < width; ++x) {
                uint8_t *pixel = bytes + y * readback_row + x * 4;
                if (pixel[0] != (uint8_t)x || pixel[1] != (uint8_t)y ||
                    pixel[2] != 0x5a || pixel[3] != 0xff) { passed = false; break; }
            }
        }
    }
    CFRelease(wrapped);
    CVMetalTextureCacheFlush(cache, 0);
    return passed;
}

static int self_test(id<MTLDevice> device, id<MTLCommandQueue> queue, CVMetalTextureCacheRef cache)
{
    NSDictionary *attributes = @{
        (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
        (id)kCVPixelBufferWidthKey: @16, (id)kCVPixelBufferHeightKey: @16,
        (id)kCVPixelBufferMetalCompatibilityKey: @YES,
        (id)kCVPixelBufferIOSurfacePropertiesKey: @{}
    };
    CVPixelBufferRef pixels = NULL;
    if (CVPixelBufferCreate(kCFAllocatorDefault, 16, 16, kCVPixelFormatType_32BGRA,
                            (__bridge CFDictionaryRef)attributes, &pixels) != kCVReturnSuccess) return 1;
    if (CVPixelBufferLockBaseAddress(pixels, 0) != kCVReturnSuccess) { CVPixelBufferRelease(pixels); return 1; }
    uint8_t *base = CVPixelBufferGetBaseAddress(pixels);
    size_t row = CVPixelBufferGetBytesPerRow(pixels);
    for (size_t y = 0; y < 16; ++y) for (size_t x = 0; x < 16; ++x) {
        uint8_t *pixel = base + y * row + x * 4;
        pixel[0] = (uint8_t)x; pixel[1] = (uint8_t)y; pixel[2] = 0x5a; pixel[3] = 0xff;
    }
    CVPixelBufferUnlockBaseAddress(pixels, 0);
    double encode = 0, complete = 0, gpu = 0;
    bool passed = copy_buffer_on_gpu(pixels, device, queue, cache, 16, 16, true, &encode, &complete, &gpu);
    CVPixelBufferRelease(pixels);
    printf("{\"mode\":\"self-test\",\"pass\":%s,\"encode_ms\":%.3f,\"complete_ms\":%.3f,\"gpu_ms\":%.3f}\n",
           passed ? "true" : "false", encode, complete, gpu);
    return passed ? 0 : 1;
}

static int capture_test(CGDirectDisplayID display_id, int count, CapturePath path,
                        int hold_seconds, NSString *png_prefix,
                        id<MTLDevice> device, id<MTLCommandQueue> queue, CVMetalTextureCacheRef cache)
{
    if (!CGPreflightScreenCaptureAccess()) {
        fprintf(stderr, "Screen Recording permission absent; no permission requested\n");
        return 77;
    }
    CGRect bounds = CGDisplayBounds(display_id);
    // Match snapshot.m exactly; CGDisplayPixelsWide can report logical pixels
    // for a scaled display, whereas the active mode exposes capture pixels.
    CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display_id);
    if (!mode) return 64;
    size_t width = CGDisplayModeGetPixelWidth(mode), height = CGDisplayModeGetPixelHeight(mode);
    CGDisplayModeRelease(mode);
    if (!width || !height || width > 16384 || height > 16384 || width > SIZE_MAX / height / 4 ||
        width * height * 4 > 512UL * 1024 * 1024) return 64;
    double discovery_ms = 0;
    SCDisplay *display = find_display(display_id, &discovery_ms);
    if (!display) return 1;
    SCContentFilter *filter = [[SCContentFilter alloc] initWithDisplay:display excludingWindows:@[]];
    filter.includeMenuBar = YES;
    SCStreamConfiguration *config = [SCStreamConfiguration new];
    config.width = width;
    config.height = height;
    config.pixelFormat = kCVPixelFormatType_32BGRA;
    config.captureDynamicRange = SCCaptureDynamicRangeSDR;
    config.showsCursor = YES;
    config.capturesAudio = NO;
    config.queueDepth = 1;
    CGRect content = filter.contentRect;
    pid_t ws_pid = windowserver_pid();
    printf("{\"mode\":\"setup\",\"path\":\"%s\",\"display\":%u,\"width\":%zu,\"height\":%zu,\"global_rect\":[%.1f,%.1f,%.1f,%.1f],\"filter_rect\":[%.1f,%.1f,%.1f,%.1f],\"discovery_ms\":%.3f,\"filter_scale\":%.3f}\n",
           path == PATH_RECT ? "rect" : path == PATH_FILTER ? "filter" : "mixed",
           display_id, width, height, bounds.origin.x, bounds.origin.y, bounds.size.width, bounds.size.height,
           content.origin.x, content.origin.y, content.size.width, content.size.height,
           discovery_ms, filter.pointPixelScale);
    fflush(stdout);
    sample_memory("before_load", 0, ws_pid, true);
    for (int i = 0; i < count; ++i) @autoreleasepool {
        // Reverse order on alternate pairs; both paths are single-shot.
        int requests = path == PATH_MIXED ? 2 : 1;
        for (int j = 0; j < requests; ++j) {
            bool filtered = path == PATH_FILTER || (path == PATH_MIXED && ((i + j) & 1) != 0);
            double acquire = 0, prepare = 0, complete = 0, gpu = 0;
            bool valid = false;
            bool png_saved = false;
            size_t observed_width = 0, observed_height = 0;
            OSType observed_format = 0;
            bool has_iosurface = false;
            char color[96] = "unknown";
            bool has_color = false, icc_attachment = false;
            size_t icc_bytes = 0;
            unsigned long long icc_hash = 0;
            if (filtered) {
                CMSampleBufferRef sample = capture_filtered(filter, config, &acquire);
                if (sample) {
                    CVPixelBufferRef pixels = CMSampleBufferGetImageBuffer(sample);
                    if (pixels) {
                        observed_width = CVPixelBufferGetWidth(pixels);
                        observed_height = CVPixelBufferGetHeight(pixels);
                        observed_format = CVPixelBufferGetPixelFormatType(pixels);
                        has_iosurface = CVPixelBufferGetIOSurface(pixels) != NULL;
                        icc_attachment = CVBufferHasAttachment(pixels, kCVImageBufferICCProfileKey);
                        has_color = color_details(CVImageBufferGetColorSpace(pixels), color, &icc_bytes, &icc_hash);
                    }
                    valid = copy_buffer_on_gpu(pixels, device, queue, cache, width, height,
                                               false, &prepare, &complete, &gpu);
                    if (png_prefix && i == 0 && valid) {
                        png_saved = save_buffer_png(pixels, [png_prefix stringByAppendingString:@"-filtered.png"]);
                        valid = png_saved;
                    }
                    CFRelease(sample);
                }
            } else {
                CGImageRef image = capture_rectangle(bounds, width, height, &acquire);
                if (image) {
                    observed_width = CGImageGetWidth(image);
                    observed_height = CGImageGetHeight(image);
                    has_color = color_details(CGImageGetColorSpace(image), color, &icc_bytes, &icc_hash);
                    valid = draw_cgimage(image, width, height, &prepare);
                    if (png_prefix && i == 0 && valid) {
                        png_saved = save_png(image, [png_prefix stringByAppendingString:@"-rect.png"]);
                        valid = png_saved;
                    }
                    CGImageRelease(image);
                }
            }
            printf("{\"mode\":\"capture\",\"pair\":%d,\"path\":\"%s\",\"valid\":%s,\"width\":%zu,\"height\":%zu,\"pixel_format\":%u,\"iosurface\":%s,\"color_present\":%s,\"icc_attachment\":%s,\"color\":\"%s\",\"icc_bytes\":%zu,\"icc_hash\":\"%016llx\",\"acquire_ms\":%.3f,\"prepare_ms\":%.3f,\"gpu_complete_ms\":%.3f,\"gpu_ms\":%.3f,\"png_saved\":%s}\n",
                   i + 1, filtered ? "filtered-buffer-metal" : "rect-cgimage-bitmap",
                   valid ? "true" : "false", observed_width, observed_height,
                   observed_format, has_iosurface ? "true" : "false", has_color ? "true" : "false",
                   icc_attachment ? "true" : "false", color, icc_bytes, icc_hash,
                   acquire, prepare, complete, gpu, png_saved ? "true" : "false");
            fflush(stdout);
            if (!valid) return 1;
            sample_memory("after_capture", 0, ws_pid, false);
            usleep(300000);
        }
    }
    sample_memory("after_load", 0, ws_pid, true);
    hold_with_samples(hold_seconds, ws_pid);
    return 0;
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        if (argc != 2 && !(argc == 3 && !strcmp(argv[1], "memory-smoke")) && (argc < 4 || argc > 7)) {
            fprintf(stderr, "usage: %s self-test | memory-smoke SECONDS | capture DISPLAY_ID COUNT(1..60) [HOLD_SECONDS(0..120) [PNG_PREFIX]] [--path=mixed|rect|filter]\n", argv[0]);
            return 64;
        }
        if (argc == 3 && !strcmp(argv[1], "memory-smoke")) {
            char *end = NULL;
            long hold = strtol(argv[2], &end, 10);
            if (!end || *end || hold < 1 || hold > 120) return 64;
            pid_t ws_pid = windowserver_pid();
            sample_memory("before_load", 0, ws_pid, true);
            hold_with_samples((int)hold, ws_pid);
            return 0;
        }
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        id<MTLCommandQueue> queue = [device newCommandQueue];
        CVMetalTextureCacheRef cache = NULL;
        if (!device || !queue || CVMetalTextureCacheCreate(kCFAllocatorDefault, NULL, device, NULL, &cache) != kCVReturnSuccess) {
            fprintf(stderr, "Metal texture cache unavailable\n");
            return 1;
        }
        int result = 64;
        if (argc == 2 && !strcmp(argv[1], "self-test")) result = self_test(device, queue, cache);
        if (argc >= 4 && !strcmp(argv[1], "capture")) {
            char *end = NULL;
            unsigned long value = strtoul(argv[2], &end, 10);
            if (end && !*end && value > 0 && value <= UINT32_MAX) {
                char *count_end = NULL;
                long count = strtol(argv[3], &count_end, 10);
                if (count_end && !*count_end && count >= 1 && count <= 60) {
                    long hold = 0;
                    int positional = 0;
                    NSString *png_prefix = nil;
                    CapturePath path = PATH_MIXED;
                    bool valid = true;
                    for (int i = 4; i < argc; ++i) {
                        if (!strncmp(argv[i], "--path=", 7)) {
                            const char *name = argv[i] + 7;
                            if (!strcmp(name, "rect")) path = PATH_RECT;
                            else if (!strcmp(name, "filter")) path = PATH_FILTER;
                            else if (!strcmp(name, "mixed")) path = PATH_MIXED;
                            else valid = false;
                        } else if (positional++ == 0) {
                            char *hold_end = NULL;
                            hold = strtol(argv[i], &hold_end, 10);
                            if (!hold_end || *hold_end || hold < 0 || hold > 120) valid = false;
                        } else if (positional == 2) {
                            png_prefix = [NSString stringWithUTF8String:argv[i]];
                            if (!png_prefix) valid = false;
                        } else valid = false;
                    }
                    if (valid) {
                        if (@available(macOS 26.0, *)) result = capture_test((CGDirectDisplayID)value, (int)count,
                            path, (int)hold, png_prefix, device, queue, cache);
                        else result = 78;
                    }
                }
            }
        }
        CFRelease(cache);
        return result;
    }
}
