#ifndef NAVIGATION_ADMISSION_H
#define NAVIGATION_ADMISSION_H

#include "../core_types.h"

// Navigation admission (admission.c): `space --navigate focus next|prev|NUMBER`
// relative requests that arrive while one waits for the event loop join it
// and are answered at once. Numeric requests carry separate timing metadata.
//
// Threads: the accept thread recognises and joins requests, and asks the HID
// system when a key was last released; the event loop claims a request's
// group before reading the request.
// State: g_space_navigation_queue, the groups waiting, under its mutex, and
// g_space_navigation_claim, the steps of the request the event loop handles,
// which only the event loop touches. Numeric requests carry timing only and
// never join or change relative steps.
// Callers: command, while it parses the claimed request. Accepting and
// claiming are hooks, see hooks.h.

// The steps the request being handled on the event loop carries: those of
// the requests that joined it, whether a key repeat started its group, and
// the shortest gap inside that group. First and last ingress times let command
// compare it with the previous validated, accepted focus request.
struct space_navigation_claim
{
    bool active;       // A relative group, including one whose steps cancel out.
    int steps;
    bool repeat;
    uint64_t gap;      // Shortest gap inside a relative group, or UINT64_MAX.
    uint64_t first_time;
    uint64_t time;     // Last ingress in this group; 0 when there is no metadata.
};

static struct space_navigation_claim space_navigation_queue_claimed(void);

#endif
