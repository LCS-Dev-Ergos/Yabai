#import <AppKit/AppKit.h>
#import <QuartzCore/CADisplayLink.h>

// AppKit objects belong to the main run loop. SkyLight writes remain on the
// worker: a busy worker must never make a display callback block Dock's UI.
API_AVAILABLE(macos(14.0))
@interface YabaiFadeDisplay : NSObject
@property(nonatomic) uint32_t display;
@property(nonatomic, retain) CADisplayLink *link;
- (void)frame:(CADisplayLink *)link;
@end

static NSMutableDictionary *window_fade_displays;

@implementation YabaiFadeDisplay
- (void)dealloc
{
    [_link release];
    [super dealloc];
}

- (void)frame:(CADisplayLink *)link
{
    if (pthread_mutex_trylock(&window_fade_lock) != 0) return;

    bool active = false;
    for (struct window_fade_context *fade = window_fades; fade; fade = fade->next) {
        if (fade->display != self.display) continue;

        active = true;
        fade->display_paced = true;
        fade->frame_ready = true;
    }

    if (active) pthread_cond_signal(&window_fade_cond);
    pthread_mutex_unlock(&window_fade_lock);

    if (!active) {
        // The dictionary and display link both retain this target.
        [self retain];
        [self.link invalidate];
        self.link = nil;
        [window_fade_displays removeObjectForKey:@(self.display)];
        [self release];
    }
}
@end

// Also clean up when a display stops delivering callbacks (sleep/unplug).
// Recheck under the lock so a queued cleanup cannot stop a newer effect.
static void window_fade_display_stop(uint32_t display)
{
    if (@available(macOS 14.0, *)) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (pthread_mutex_trylock(&window_fade_lock) != 0) {
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 50000000), dispatch_get_main_queue(), ^{
                    window_fade_display_stop(display);
                });
                return;
            }

            bool active = false;
            for (struct window_fade_context *fade = window_fades; fade; fade = fade->next) {
                if (fade->display == display) active = true;
            }
            pthread_mutex_unlock(&window_fade_lock);
            if (active) return;

            YabaiFadeDisplay *target = window_fade_displays[@(display)];
            [target.link invalidate];
            target.link = nil;
            [window_fade_displays removeObjectForKey:@(display)];
        });
    }
}

static void window_fade_display_start(uint32_t display)
{
    if (@available(macOS 14.0, *)) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!window_fade_displays) window_fade_displays = [[NSMutableDictionary alloc] init];
            if (window_fade_displays[@(display)]) return;

            for (NSScreen *screen in [NSScreen screens]) {
                if ([screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue] != display) continue;

                YabaiFadeDisplay *target = [[YabaiFadeDisplay alloc] init];
                target.display = display;
                target.link = [screen displayLinkWithTarget:target selector:@selector(frame:)];
                if (target.link) {
                    window_fade_displays[@(display)] = target;
                    [target.link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
                }

                [target release];
                break;
            }
        });
    }
}
