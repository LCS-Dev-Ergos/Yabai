#ifndef EFFECTS_SNAPSHOT_H
#define EFFECTS_SNAPSHOT_H

// Snapshot crossfade (snapshot.m, with snapshot_capture.m and
// snapshot_surface.m): a capture of the outgoing Desktop, shown in a window
// of our own above an ordinary Dock switch, fades out.
//
// Threads: the event loop prepares, presents, starts and cancels. Preparing
// synchronously waits for the capture (up to 150 ms); capturing
// asynchronously returns at once, and the capture's callback or its deadline
// reports back through space_navigation_snapshot_captured. Presenting the
// image waits for one refresh. ScreenCaptureKit completes the capture on its
// own queue. The overlay's alpha timer runs on a global queue, and its cancel
// handler frees the snapshot there.
// State: space_snapshot_active and the auxiliary Spaces created last, under
// space_snapshot_lock; the capture in flight, under
// space_snapshot_captures.lock; the asynchronous request waiting for its
// capture, space_snapshot_pending, on the event loop only.
// Callers: step (prepare, or capture and present, before the switch; start
// after it). Cancellation and the Space notifications are hooks, see hooks.h.
// Calls: space_navigation_snapshot_captured in command.c, from the capture's
// queue and from a global queue at the deadline. Tests supply it.

// What became of an asynchronous capture when its step asks to present it.
enum space_snapshot_result
{
    SPACE_SNAPSHOT_CANCELLED,   // Cancelled while it was captured: the step stops.
    SPACE_SNAPSHOT_MISSING,     // No usable image: the step switches without it.
    SPACE_SNAPSHOT_READY        // The image covers the display, ready to fade.
};

static bool space_navigation_snapshot_prepare(uint32_t display, uint64_t target, float interval);
static bool space_navigation_snapshot_capture(uint32_t display, uint64_t target, float interval, int token);
static enum space_snapshot_result space_navigation_snapshot_present(int token);
static bool space_navigation_snapshot_start(float duration, bool switched);

#endif
