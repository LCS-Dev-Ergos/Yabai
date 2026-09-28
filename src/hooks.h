#ifndef HOOKS_H
#define HOOKS_H

// Where the window manager's core calls the navigation, effects and focus
// modules, grouped by the file and handler that calls: the event handlers,
// the command handler, the accept thread and the AX observers. A change at
// one of these places has to keep the call and its position. The modules
// describe their threads and state in their own headers under navigation/
// and effects/.

// application.c, main thread, AX focused-window notification: the window
// becomes the observed focus that activation can use without asking the
// application. The navigation step notes the windows it activates too.
static void window_focus_note(uint32_t window_id);

// event_loop.c, APPLICATION_FRONT_SWITCHED: the front application's focus,
// from the observed window when it is still valid.
static void window_manager_handle_front_focus(struct application *application);

// event_loop.c, WINDOW_FOCUSED: clears the observation it reports, and
// releases queued navigation waiting for the window its last step activated.
static void window_focus_consume(uint32_t window_id);
static void space_navigation_schedule_focused(uint32_t window_id);

// event_loop.c, SLS_SPACE_CREATED: the crossfade's auxiliary Spaces are not
// Desktops and need no view.
static bool space_navigation_snapshot_owns_space(uint64_t sid);

// event_loop.c, SPACE_CHANGED: a crossfade ends once its display leaves the
// Desktop it switched to.
static void space_navigation_snapshot_space_changed(void);

// event_loop.c, DISPLAY_ADDED, DISPLAY_REMOVED, DISPLAY_MOVED,
// DISPLAY_RESIZED, MOUSE_DOWN, MISSION_CONTROL_SHOW_ALL_WINDOWS,
// MISSION_CONTROL_SHOW_FRONT_WINDOWS, MISSION_CONTROL_SHOW_DESKTOP,
// MISSION_CONTROL_ENTER, DOCK_DID_RESTART and SYSTEM_WOKE: any of them ends a
// crossfade. The navigation step and command cancel it as well.
static void space_navigation_snapshot_cancel(void);

// event_loop.c, DAEMON_MESSAGE, before the request is read: the request takes
// the steps of the requests that joined it.
static void space_navigation_queue_claim(int sockfd);

// event_loop.c, the navigation events SPACE_NAVIGATION_FOCUS and
// SPACE_NAVIGATION_DISPATCH.
static void space_navigation_focus_resume(int generation);
static void space_navigation_schedule_timer(void);

// event_loop.c, event_loop_run after each event: signposts slow events.
static void event_loop_trace(enum event_type type, uint64_t started);

// message.c, config space_navigation_pacing. Command reads the setting too.
static bool space_navigation_schedule_pacing(void);
static void space_navigation_schedule_set_pacing(bool pacing);

// message.c, handle_domain_space: the Desktop relative navigation starts
// from, and the `--navigate` command.
static uint64_t space_navigation_active_space(void);
static void space_navigation_command(FILE *rsp, char **message);

// message.c, handle_message, before any command: every command but a query
// or a navigation ends the anchor, the deferred focus, the crossfade and the
// queued navigation.
static void space_navigation_note_message(char *message);

// message.c, message_loop_run, accept thread: a relative request joins the
// one waiting and is answered here instead of being posted.
static bool space_navigation_accept(int sockfd);

// The other direction: file-static functions of the core that the modules
// call. window_manager.c comes after the navigation sources. message.c's
// parser (get_token, token_equals, token_to_value, parse_space_selector,
// daemon_fail), mission_control_is_active and event_loop.c's
// window_did_receive_focus precede their callers.
static void window_manager_make_key_window(ProcessSerialNumber *window_psn, uint32_t window_id);

#endif
