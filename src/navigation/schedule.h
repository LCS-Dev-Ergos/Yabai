#ifndef NAVIGATION_SCHEDULE_H
#define NAVIGATION_SCHEDULE_H

#include "../core_types.h"

// Navigation schedule (schedule.c): queued navigation runs one Desktop at a
// time, paced by a rhythm, the effect's duration and the last activation. A
// Desktop reached without activation, with nothing queued after it, takes
// focus once no request has come for a moment.
//
// Thread: event loop. Its wakes arrive as SPACE_NAVIGATION_DISPATCH events.
// State: g_space_navigation_schedule, the queued switches, when the last step
// ended, the effect and activation guards, the earliest wake requested and
// whether a step is still running.
// A step that waits for its capture keeps the schedule running: no other
// step starts until it reports its end.
// Callers: command queues, pumps and cancels; the step asks whether to leave
// its activation to steps queued since it started, and reports the window it
// activated, the effect Dock accepted and the end of a step that waited for
// its capture. The wake, focus changes and the pacing setting are hooks, see
// hooks.h.
// Calls: space_navigation_execute and space_navigation_schedule_after in
// command.c, which start a step and request a wake, and the time since the
// last click from the step. Tests supply all three.

// Presses closer than this make a quick burst, whose switches show no effect.
// Recorded on this host, presses meant to look at each Desktop came 450-600 ms
// apart, and quick runs 150-350 ms apart.
#define SPACE_NAVIGATION_FAST_NS 400000000ULL

struct space_navigation_request
{
    bool move;
    int steps;          // Desktops forward (positive) or back; 0 for `sid`.
    bool jump;          // Moves all its steps in one switch, from `sid` if set.
    bool repeat;        // Queued by a held key.
    bool fast;          // Pressed within SPACE_NAVIGATION_FAST_NS of the press before.
    uint64_t sid;       // With neither steps nor sid: the Desktop navigation reached.
    bool crossfade;     // The overlay effects exclude each other and the window fade.
    bool veil;
    float alpha;
    float duration;
    uint64_t time;      // First ingress, or queue time for internal requests.
};

static bool space_navigation_schedule_add(struct space_navigation_request *request, bool repeat);
static void space_navigation_schedule_pump(void);
static void space_navigation_schedule_cancel(void);
static void space_navigation_schedule_activated(uint32_t window_id);
static void space_navigation_schedule_switched(float duration);
static bool space_navigation_schedule_defers_activation(void);
static void space_navigation_schedule_completed(bool success);

#endif
