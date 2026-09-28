#ifndef NAVIGATION_SCHEDULE_H
#define NAVIGATION_SCHEDULE_H

// Navigation schedule (schedule.c): queued navigation runs one Desktop at a
// time, paced by a rhythm, the effect's duration and the last activation.
//
// Thread: event loop. Its wakes arrive as SPACE_NAVIGATION_DISPATCH events.
// State: g_space_navigation_schedule, the queued switches, when the last step
// ended, the effect and activation guards, the earliest wake requested and
// whether a step is still running.
// A step that waits for its capture keeps the schedule running: no other
// step starts until it reports its end.
// Callers: command queues, pumps and cancels; the step reports the window it
// activated, the effect Dock accepted and the end of a step that waited for
// its capture. The wake, focus changes and the pacing setting are hooks, see
// hooks.h.
// Calls: space_navigation_execute and space_navigation_schedule_after in
// command.c, which start a step and request a wake, and the time since the
// last click from the step. Tests supply all three.

struct space_navigation_request
{
    bool move;
    int steps;          // Desktops forward (positive) or back; 0 for `sid`.
    bool jump;          // Moves all its steps in one switch, from `sid` if set.
    bool repeat;        // Queued by a held key.
    uint64_t sid;
    bool crossfade;
    float alpha;
    float duration;
    uint64_t time;      // When it was queued.
};

static bool space_navigation_schedule_add(struct space_navigation_request *request, bool repeat);
static void space_navigation_schedule_pump(void);
static void space_navigation_schedule_cancel(void);
static void space_navigation_schedule_activated(uint32_t window_id);
static void space_navigation_schedule_switched(float duration);
static void space_navigation_schedule_completed(bool success);

#endif
