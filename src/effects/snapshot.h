#ifndef EFFECTS_SNAPSHOT_H
#define EFFECTS_SNAPSHOT_H

#include "../core_types.h"

// Snapshot crossfade and veil (snapshot.m, with snapshot_capture.m and
// snapshot_surface.m): a window of our own above an ordinary Dock switch fades out.
// The crossfade shows a capture of the outgoing Desktop; the veil shows solid black
// at a fixed opacity and captures nothing, so it needs no Screen Recording
// permission.
//
// Threads: the event loop prepares, presents, starts and cancels. Preparing
// synchronously waits for the capture (up to 150 ms); capturing asynchronously
// returns at once, and the capture's callback or its deadline reports back through
// space_navigation_snapshot_captured. Presenting the image waits for one refresh,
// the veil for two; a veil with a background blur first fades in over 100 ms, about
// 130 ms in all at 60 Hz. ScreenCaptureKit completes the capture on its own queue.
// The overlay's alpha timer runs on a global queue, and its cancel handler frees the
// snapshot there.
// State: space_snapshot_active and the auxiliary Spaces created last, under
// space_snapshot_lock; the capture in flight, under space_snapshot_captures.lock;
// the asynchronous request waiting for its capture, space_snapshot_pending, on the
// event loop only. The fade curve and the veil's blur radius are read from
// g_window_manager on the event loop, when the overlay is created.
// Callers: step (prepare, or capture and present, or the veil, before the switch;
// start after it). Cancellation and the Space notifications are hooks, see hooks.h.
// Calls: space_navigation_snapshot_captured in command.c, from the capture's queue
// and from a global queue at the deadline. Tests supply it.

// How the overlay's opacity falls over the fade (config navigation_fade_curve).
// Smooth starts slowly; ease-out changes visibly from the first frame.
enum space_snapshot_curve
{
    SPACE_SNAPSHOT_CURVE_SMOOTH,
    SPACE_SNAPSHOT_CURVE_EASE_OUT,
    SPACE_SNAPSHOT_CURVE_COUNT
};

static char *space_snapshot_curve_str[] =
{
    [SPACE_SNAPSHOT_CURVE_SMOOTH]   = "smooth",
    [SPACE_SNAPSHOT_CURVE_EASE_OUT] = "ease_out"
};

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
// Drop only this token's pending presentation; its framework callback retains
// ownership and its unresolved slot until it really returns. Active overlays stay.
static enum space_snapshot_result space_navigation_snapshot_discard(int token);
// Event loop: shows a solid black veil over the display without capturing it,
// ordered in invisible and raised to its opacity a refresh later, and waits two more
// refreshes for it to reach the screen. With navigation_veil_blur above 0 and Reduce
// Transparency off, the veil blurs what lies below it and is faded in completely
// first. False when none is shown.
static bool space_navigation_veil_prepare(uint32_t display, uint64_t target, float interval);
// Starts the fade of the overlay prepared or presented, once Dock switched.
static bool space_navigation_snapshot_start(float duration, bool switched);

#endif
