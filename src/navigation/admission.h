#ifndef NAVIGATION_ADMISSION_H
#define NAVIGATION_ADMISSION_H

// Navigation admission (admission.c): `space --navigate focus next|prev`
// requests that arrive while one waits for the event loop join it and are
// answered at once.
//
// Threads: the accept thread recognises and joins requests, and asks the HID
// system when a key was last released; the event loop claims a request's
// group before reading the request.
// State: g_space_navigation_queue, the groups waiting, under its mutex, and
// g_space_navigation_claim, the steps of the request the event loop handles,
// which only the event loop touches.
// Callers: command, while it parses the claimed request. Accepting and
// claiming are hooks, see hooks.h.

// The steps the request being handled on the event loop carries: those of
// the requests that joined it, and whether a key repeat started its group.
struct space_navigation_claim
{
    bool active;
    int steps;
    bool repeat;
};

static struct space_navigation_claim space_navigation_queue_claimed(void);

#endif
