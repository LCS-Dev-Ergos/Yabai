#ifndef HOOKS_H
#define HOOKS_H

#include "core_types.h"

// Where the window manager's core calls the navigation, effects and focus
// modules, grouped by the file and handler that calls: the event handlers,
// the command handler, the accept thread and the AX observers. A change at
// one of these places has to keep the call and its position. The modules
// describe their threads and state in their own headers under navigation/
// and effects/.

// application.c, main thread, AX focused-window notification: the window
// becomes the observed focus that activation can use without asking the
// application. The navigation step notes the windows it activates too.
void window_focus_note(uint32_t window_id);

// events/handlers/applications.c, APPLICATION_FRONT_SWITCHED: the front
// application's focus, from the observed window when it is still valid.
static void window_manager_handle_front_focus(struct application *application);

// events/handlers/windows.c, WINDOW_FOCUSED: clears the observation it reports
// and releases queued navigation waiting for the window its last step activated.
static void window_focus_consume(uint32_t window_id);
void space_navigation_schedule_focused(uint32_t window_id);

// events/handlers/spaces.c, SLS_SPACE_CREATED: the crossfade's and veil's auxiliary
// Spaces are not Desktops and need no view.
bool space_navigation_snapshot_owns_space(uint64_t sid);

// events/handlers/spaces.c, SPACE_CHANGED: a crossfade or veil ends once its display
// leaves the Desktop it switched to.
void space_navigation_snapshot_space_changed(void);

// events/handlers/displays.c (display changes), mouse.c (MOUSE_DOWN),
// mission_control.c (Mission Control), and system.c (Dock restart and wake):
// these handlers end a crossfade or veil. The navigation step and command do too.
void space_navigation_snapshot_cancel(void);

// events/handlers/messages.c, DAEMON_MESSAGE, before the request is read:
// the request takes the steps of the requests that joined it.
void space_navigation_queue_claim(int sockfd);

// events/handlers/messages.c, the navigation events SPACE_NAVIGATION_FOCUS,
// SPACE_NAVIGATION_DISPATCH and SPACE_NAVIGATION_CAPTURED.
void space_navigation_focus_resume(int generation);
void space_navigation_schedule_timer(void);
void space_navigation_step_captured(int token);

// event_loop.c, event_loop_run after each event: signposts slow events.
static void event_loop_trace(enum event_type type, uint64_t started);

// ipc/commands/config.c, space_navigation_pacing. Command reads the setting too.
bool space_navigation_schedule_pacing(void);
void space_navigation_schedule_set_pacing(bool pacing);

// ipc/commands/space.c, handle_domain_space: the Desktop relative navigation starts
// from, and the `--navigate` command.
uint64_t space_navigation_active_space(void);
void space_navigation_command(FILE *rsp, char **message);

// ipc/message_loop.c, handle_message, before any command: every command but a query
// or a navigation ends the anchor, the deferred focus, the crossfade or veil and the
// queued navigation.
void space_navigation_note_message(char *message);

// ipc/message_loop.c, message_loop_run, accept thread: a relative request joins the
// one waiting and is answered here instead of being posted.
bool space_navigation_accept(int sockfd);

#endif
