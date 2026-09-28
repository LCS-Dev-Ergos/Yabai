// The outgoing frame: one ScreenCaptureKit screenshot of the display, usable
// when it arrives within SPACE_SNAPSHOT_CAPTURE_NS. A synchronous preparation
// waits for it on the event loop; an asynchronous one learns of it, or of the
// deadline, through space_navigation_snapshot_captured.
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#define SPACE_SNAPSHOT_CAPTURE_NS 150000000ULL

// How long a capture may stay without its callback before it counts as lost.
#ifndef SPACE_SNAPSHOT_STALE_NS
#define SPACE_SNAPSHOT_STALE_NS 2000000000ULL
#endif

struct space_snapshot_capture
{
    int references;
    uint64_t generation;
    int token;                  // Reported when the image arrives; 0 for none.
    dispatch_semaphore_t ready;
    dispatch_time_t deadline;
    uint64_t began;             // Before the capture was requested.
    uint64_t returned;          // When the request call returned.
    uint64_t arrived;           // When the callback stored the image.
    CGImageRef image;
};

// A timed-out capture remains the only request in flight until its callback
// arrives, so slow WindowServer responses cannot build a backlog. A callback
// that never arrived would disable the crossfade until yabai restarts: after
// SPACE_SNAPSHOT_STALE_NS the request counts as lost and another may start.
// Its callback, if it still comes, only releases its own image.
static struct
{
    pthread_mutex_t lock;
    uint64_t generation;
    uint64_t pending;       // The capture in flight, 0 when none.
    uint64_t started;
} space_snapshot_captures = { .lock = PTHREAD_MUTEX_INITIALIZER };

static uint64_t space_snapshot_capture_begin(void)
{
    uint64_t generation = 0;
    uint64_t now = read_os_timer();

    pthread_mutex_lock(&space_snapshot_captures.lock);

    if (!space_snapshot_captures.pending || now - space_snapshot_captures.started >= SPACE_SNAPSHOT_STALE_NS) {
        generation = ++space_snapshot_captures.generation;
        space_snapshot_captures.pending = generation;
        space_snapshot_captures.started = now;
    }

    pthread_mutex_unlock(&space_snapshot_captures.lock);

    return generation;
}

static void space_snapshot_capture_end(uint64_t generation)
{
    pthread_mutex_lock(&space_snapshot_captures.lock);

    if (space_snapshot_captures.pending == generation) {
        space_snapshot_captures.pending = 0;
    }

    pthread_mutex_unlock(&space_snapshot_captures.lock);
}

static void space_snapshot_capture_release(struct space_snapshot_capture *capture)
{
    if (__sync_sub_and_fetch(&capture->references, 1)) return;
    if (capture->image) CGImageRelease(capture->image);
    dispatch_release(capture->ready);
    free(capture);
}

// Requests a capture of `bounds`. NULL when the system cannot capture or a
// capture is still in flight. The caller owns one reference.
static struct space_snapshot_capture *space_snapshot_capture_start(CGRect bounds, int token)
{
    if (@available(macOS 15.2, *)) { } else return NULL;

    uint64_t generation = space_snapshot_capture_begin();
    if (!generation) return NULL;

    struct space_snapshot_capture *capture = calloc(1, sizeof(*capture));
    if (!capture) {
        space_snapshot_capture_end(generation);
        return NULL;
    }

    capture->references = 2; // Caller and asynchronous completion.
    capture->generation = generation;
    capture->token = token;
    capture->ready = dispatch_semaphore_create(0);
    if (!capture->ready) {
        free(capture);
        space_snapshot_capture_end(generation);
        return NULL;
    }

    capture->began = read_os_timer();
    capture->deadline = dispatch_time(DISPATCH_TIME_NOW, SPACE_SNAPSHOT_CAPTURE_NS);
    if (@available(macOS 15.2, *)) {
        [SCScreenshotManager captureImageInRect:bounds completionHandler:^(CGImageRef image, NSError *error) {
            (void) error;
            capture->image = image ? CGImageRetain(image) : NULL;
            capture->arrived = read_os_timer();
            space_snapshot_capture_end(capture->generation);
            dispatch_semaphore_signal(capture->ready);
            if (capture->token) space_navigation_snapshot_captured(capture->token);
            space_snapshot_capture_release(capture);
        }];
    }
    capture->returned = read_os_timer();

    return capture;
}

// The captured image, retained, or NULL when it is missing or came too late.
// With `wait` this waits for it up to the deadline; otherwise the callback
// must have come already, and counts as on time if both it and the request
// call finished within SPACE_SNAPSHOT_CAPTURE_NS, however late we look.
static CGImageRef space_snapshot_capture_take(struct space_snapshot_capture *capture, bool wait)
{
    if (dispatch_semaphore_wait(capture->ready, wait ? capture->deadline : DISPATCH_TIME_NOW) != 0) return NULL;

    uint64_t finished = wait ? read_os_timer() : capture->arrived > capture->returned ? capture->arrived : capture->returned;
    if (finished - capture->began > SPACE_SNAPSHOT_CAPTURE_NS) return NULL;

    return capture->image ? CGImageRetain(capture->image) : NULL;
}
