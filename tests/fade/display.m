// Opt-in host probe: real AppKit display links, simulated alpha writes.
// It creates no windows and never calls SkyLight or changes the desktop.
#define FADE_DISPLAY_LINK
#include "fixture.h"

static void run_loop_for(double seconds)
{
    NSDate *end = [NSDate dateWithTimeIntervalSinceNow:seconds];
    while ([end timeIntervalSinceNow] > 0.0) {
        [[NSRunLoop mainRunLoop] runMode:NSDefaultRunLoopMode
                            beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.002]];
    }
}

int main(void)
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        if (![NSScreen screens].count) return 77;

        for (NSScreen *screen in [NSScreen screens]) {
            uint32_t display = [screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue];
            struct sa_window_opacity windows[] = { {1, .9f}, {2, .9f} };
            assert(window_fade_batch(display, SA_OPACITY_PREPARE, .9f, 0, 1.0f / 60, windows, 2));
            windows[0].alpha = 1.0f;
            windows[1].alpha = .975f;
            int before = alpha_calls;
            assert(window_fade_batch(display, SA_OPACITY_START, .9f, .15f, 1.0f / 60, windows, 2));
            run_loop_for(.07);

            pthread_mutex_lock(&window_fade_lock);
            assert(window_fades && window_fades->display_paced);
            pthread_mutex_unlock(&window_fade_lock);

            run_loop_for(.2);
            pthread_mutex_lock(&window_fade_lock);
            assert(!window_fades && alphas[1] == 1.0f && alphas[2] == .975f);
            int writes = alpha_calls - before;
            pthread_mutex_unlock(&window_fade_lock);
            assert(window_fade_displays.count == 0);
            printf("display %u: real callbacks received, %d writes/window, links idle\n", display, writes / 2);

            // Block this run loop to simulate a stopped display link. The
            // worker must complete and queued main-thread cleanup must drain.
            assert(window_fade_batch(display, SA_OPACITY_START, .9f, .1f, 1.0f / 60, windows, 2));
            run_loop_for(.03);
            usleep(150000);
            wait_idle();
            run_loop_for(.05);
            assert(window_fade_displays.count == 0);
        }
    }

    puts("display: callback pacing, idle cleanup and stalled-run-loop recovery passed");
    return 0;
}
