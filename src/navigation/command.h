#ifndef NAVIGATION_COMMAND_H
#define NAVIGATION_COMMAND_H

#include "../core_types.h"

// Navigation command (command.c): `space --navigate` from the request to the
// steps it queues or runs, and the host services the other navigation modules
// call.
//
// Thread: event loop. The timers below post their events from a global
// queue, and the capture report from the capture's queue as well.
// State: none of its own. It has topology read its snapshot for each request
// and each queued step, and asks admission for the claimed steps.
// Callers: the schedule starts steps and requests wakes; activation requests
// its delay; the snapshot reports its capture. The command handler calls its entry points, see hooks.h.
// Parses with the token and selector functions declared in message.h.

struct space_navigation_request;

static enum space_navigation_result space_navigation_execute(struct space_navigation_request *request, int steps,
                                                             bool activate, bool settle, float duration);
static void space_navigation_schedule_after(uint64_t delay_ns);
static void space_navigation_focus_schedule(int generation, uint64_t delay_ns);
static void space_navigation_snapshot_captured(int token);

#endif
