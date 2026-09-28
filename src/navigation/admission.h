#ifndef NAVIGATION_ADMISSION_H
#define NAVIGATION_ADMISSION_H

// Navigation admission (admission.c): `space --navigate focus next|prev`
// requests that arrive while one waits for the event loop join it and are
// answered at once.
//
// Threads: the accept thread recognises and joins requests; the event loop
// claims a request's group before reading the request.
// State: g_space_navigation_queue, the groups waiting, under its mutex, and
// g_space_navigation_claim, the steps of the request the event loop handles,
// which only the event loop touches. Command reads the claim directly.
// Callers: command's accept (accept thread). Claiming is a hook, see
// hooks.h.

static int space_navigation_request_direction(const char *bytes, int length);
static bool space_navigation_queue_join(int sockfd, int direction, uint64_t now);

#endif
