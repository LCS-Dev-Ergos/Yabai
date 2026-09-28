#ifndef NAVIGATION_COMMAND_H
#define NAVIGATION_COMMAND_H

// Navigation command (command.c): `space --navigate` from the request to the
// steps it queues or runs, and the host services the other navigation modules
// call.
//
// Thread: event loop. The timers below post their events from a global
// queue.
// State: none of its own. It has topology read its snapshot for each request
// and each queued step, and asks admission for the claimed steps.
// Callers: the schedule runs steps and requests wakes; activation requests
// its delay. The command handler calls its entry points, see hooks.h.
// manifest.m includes command.c after message.c, whose file-static token and
// selector parser command.c uses.

struct space_navigation_request;

static bool space_navigation_execute(struct space_navigation_request *request, int steps,
                                     bool activate, bool settle, float duration);
static void space_navigation_schedule_after(uint64_t delay_ns);
static void space_navigation_focus_schedule(int generation, uint64_t delay_ns);

#endif
