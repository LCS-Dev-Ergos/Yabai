#ifndef EFFECTS_SNAPSHOT_H
#define EFFECTS_SNAPSHOT_H

// Snapshot crossfade (snapshot.m, with snapshot_capture.m and
// snapshot_surface.m): a capture of the outgoing Desktop, shown in a window
// of our own above an ordinary Dock switch, fades out.
//
// Threads: the event loop prepares, starts and cancels; preparing waits for
// the capture (up to 150 ms) and one refresh. ScreenCaptureKit completes the
// capture on its own queue. The overlay's alpha timer runs on a global queue,
// and its cancel handler frees the snapshot there.
// State: space_snapshot_active and the auxiliary Spaces created last, under
// space_snapshot_lock; the capture in flight, under
// space_snapshot_captures.lock.
// Callers: step (prepare before the switch, start after it). Cancellation and
// the Space notifications are hooks, see hooks.h.

static bool space_navigation_snapshot_prepare(uint32_t display, uint64_t target, float interval);
static bool space_navigation_snapshot_start(float duration, bool switched);

#endif
