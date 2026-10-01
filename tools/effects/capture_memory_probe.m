// Opt-in, headless ScreenCaptureKit retention reproducer. Uses only public APIs.
// See docs/reports/WindowServer-Capture-Retention_7.1.25-lcs32.md.
// Build with ARC. Each READY/STEP/DONE waits for a newline so an external
// observer can measure WindowServer while this process remains alive.
// Does not save images, switch Spaces, create overlays or change the daemon.
#import <Cocoa/Cocoa.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static bool checkpoint(const char *label, int step)
{
    if (step) printf("%s %d\n", label, step);
    else printf("%s\n", label);
    fflush(stdout);
    return getchar() != EOF;
}

int main(int argc, char **argv)
{
    if (argc != 3 || (strcmp(argv[1], "direct") && strcmp(argv[1], "newrect"))) {
        fprintf(stderr, "usage: %s direct|newrect COUNT (1..12); advance checkpoints with Enter\n", argv[0]);
        return 64;
    }
    char *end = NULL;
    long count = strtol(argv[2], &end, 10);
    if (!end || *end || count < 1 || count > 12) return 64;
    if (!CGPreflightScreenCaptureAccess()) {
        fprintf(stderr, "Screen capture permission unavailable; no permission requested.\n");
        return 77;
    }
    if (@available(macOS 26.0, *)) {} else return 78;
    bool modern = !strcmp(argv[1], "newrect");
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        CGRect bounds = CGDisplayBounds(CGMainDisplayID());
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            @autoreleasepool {
                if (!checkpoint("READY", 0)) exit(0);
                for (int i = 0; i < count; ++i) {
                    @autoreleasepool {
                        dispatch_semaphore_t done = dispatch_semaphore_create(0);
                        uint64_t began = clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
                        void (^received)(CGImageRef, NSError *) = ^(CGImageRef frame, NSError *error) {
                            @autoreleasepool {
                                printf("FRAME valid=%d size=%zux%zu ms=%.1f error=%ld\n",
                                       frame != NULL, frame ? CGImageGetWidth(frame) : 0,
                                       frame ? CGImageGetHeight(frame) : 0,
                                       (clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) - began) / 1e6,
                                       (long) error.code);
                            }
                            dispatch_semaphore_signal(done);
                        };
                        if (modern) {
                            SCScreenshotConfiguration *config = [SCScreenshotConfiguration new];
                            config.showsCursor = NO;
                            config.dynamicRange = SCScreenshotDynamicRangeSDR;
                            config.displayIntent = SCScreenshotDisplayIntentLocal;
                            [SCScreenshotManager captureScreenshotWithRect:bounds configuration:config
                                completionHandler:^(SCScreenshotOutput *output, NSError *error) {
                                    received(output.sdrImage, error);
                                }];
                        } else {
                            [SCScreenshotManager captureImageInRect:bounds completionHandler:received];
                        }
                        if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC))) {
                            fprintf(stderr, "Capture timed out; exiting diagnostic process.\n");
                            exit(2);
                        }
                    }
                    usleep(700000);
                    if (!checkpoint("STEP", i + 1)) exit(0);
                }
                checkpoint("DONE", 0);
                exit(0);
            }
        });
        [NSApp run];
    }
    return 0;
}
