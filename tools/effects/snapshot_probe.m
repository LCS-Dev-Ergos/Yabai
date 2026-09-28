// Opt-in live probe of the production snapshot renderer, without replacing
// the installed daemon or payload. check_bar.py supplies the idle guard and
// restores the original Desktop/window around this helper.
#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#include <pthread.h>
#include <unistd.h>
#include <math.h>
#include "../../src/misc/extern.h"
#include "../../src/space_navigation_display.m"
static uint64_t read_os_timer(void)
{
    return clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
}
#include "../../src/space_navigation_snapshot.m"

int main(int argc, const char **argv)
{
    if (argc != 4 && argc != 5) return 64;
    char *end = NULL;
    unsigned long display_value = strtoul(argv[1], &end, 10);
    if (end == argv[1] || *end || !display_value || display_value > UINT32_MAX) return 64;
    uint32_t display = (uint32_t) display_value;
    NSString *destination = [NSString stringWithUTF8String:argv[2]];
    float duration = strtof(argv[3], &end);
    if (end == argv[3] || *end || !(duration > 0.0f && duration <= 1.0f)) return 64;
    if (argc == 5 && strcmp(argv[4], "--warm") != 0) return 64;
    bool warm = argc == 5;

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), ^{
        @autoreleasepool {
            float interval = space_navigation_frame_interval(display);
            fprintf(stderr, "snapshot interval %.2f ms\n", interval * 1000.0f);
            if (warm) {
                for (int i = 0; i < 3; ++i) {
                    uint64_t sample = read_os_timer();
                    bool ok = space_navigation_snapshot_prepare(display, 1, interval);
                    space_navigation_snapshot_cancel();
                    fprintf(stderr, "prepare sample %d ok=%d %.1f ms\n", i, ok, (read_os_timer()-sample)/1e6);
                }
            }
            uint64_t began = read_os_timer();
            bool prepared = space_navigation_snapshot_prepare(display, 1, interval);
            fprintf(stderr, "snapshot prepared=%d %.1f ms\n", prepared, (read_os_timer() - began) / 1e6);
            if (!prepared) exit(2); // The probe must exercise an actual blend.
            NSTask *task = [[NSTask alloc] init];
            task.executableURL = [NSURL fileURLWithPath:@"/run/current-system/sw/bin/yabai"];
            task.arguments = @[@"-m", @"space", @"--navigate", @"focus", destination, @"1", @"0"];
            NSError *error = nil;
            bool switched = [task launchAndReturnError:&error];
            if (switched) {
                [task waitUntilExit];
                switched = task.terminationStatus == 0;
            }
            [task release];
            bool active = space_navigation_snapshot_start(duration, switched);
            fprintf(stderr, "snapshot active=%d %.1f ms\n", active, (read_os_timer() - began) / 1e6);
            usleep((useconds_t) ((duration + .1f) * 1e6f));
            space_navigation_snapshot_cancel();
            exit(active ? 0 : 2);
        }
    });
    CFRunLoopRun();
    return 0;
}
