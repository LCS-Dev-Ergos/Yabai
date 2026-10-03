// Navigation runs within one event-loop request, without shell queries or
// global configuration edits.
#ifndef SNAP_DIAG
#define SNAP_DIAG(phase, generation, token) ((void)0)
#endif
static uint64_t space_navigation_last_time;

// The space the last navigation switched to. For a moment after a switch,
// WindowServer's active display can follow an application to a window it has
// on another display; relative navigation starts from this space meanwhile.
#define SPACE_NAVIGATION_ANCHOR_NS 1000000000ULL

static struct
{
    uint64_t sid;
    uint64_t time;
} space_navigation_anchor;

static void space_navigation_forget(void)
{
    space_navigation_anchor.sid = 0;
}

static double space_navigation_seconds_since_click(void)
{
    double left  = CGEventSourceSecondsSinceLastEventType(kCGEventSourceStateHIDSystemState, kCGEventLeftMouseDown);
    double right = CGEventSourceSecondsSinceLastEventType(kCGEventSourceStateHIDSystemState, kCGEventRightMouseDown);
    double other = CGEventSourceSecondsSinceLastEventType(kCGEventSourceStateHIDSystemState, kCGEventOtherMouseDown);

    return fmin(left, fmin(right, other));
}

// Past the anchor's lifetime, or after a click, the active display reflects
// where the user works again.
static uint64_t space_navigation_current_space(uint64_t active_sid)
{
    uint64_t sid = space_navigation_anchor.sid;
    if (!sid || sid == active_sid) return active_sid;

    uint64_t age = read_os_timer() - space_navigation_anchor.time;
    if (age >= SPACE_NAVIGATION_ANCHOR_NS || !space_navigation_space_visible(sid)) return active_sid;
    if (space_navigation_seconds_since_click() * 1e9 < (double) age) return active_sid;

    return sid;
}

static bool space_navigation_window(struct window *window)
{
    return window && window_manager_is_window_eligible(window)
        && !window->application->is_hidden
        && !window_check_flag(window, WINDOW_MINIMIZE)
        && !window_check_flag(window, WINDOW_STICKY);
}

static float space_navigation_opacity(struct window *window, uint32_t focused_id)
{
    if (window->opacity != 0.0f) return window->opacity;
    if (!g_window_manager.enable_window_opacity) return 1.0f;

    return window->id == focused_id ? g_window_manager.active_window_opacity
                                    : g_window_manager.normal_window_opacity;
}

static bool space_navigation_spaces_contain(uint64_t *spaces, int count, uint64_t sid)
{
    for (int i = 0; i < count; ++i) {
        if (spaces[i] == sid) return true;
    }

    return false;
}

// An application activated with another window visible on a different
// display, such as a Chromium browser, can make that window key instead.
// A tiled window's Desktop is the Space of its view. Only floating and
// unmanaged windows need WindowServer, which lists the application's windows
// on the Desktops visible elsewhere with one query.
static bool space_navigation_needs_raise(struct window *window, uint32_t display)
{
    uint64_t spaces[SPACE_NAVIGATION_DISPLAYS_MAX];
    int space_count = space_navigation_spaces_visible_elsewhere(display, spaces);
    if (!space_count) return false;

    int count = 0;
    struct window **list = window_manager_find_application_windows(&g_window_manager, window->application, &count);

    bool unknown = false;
    for (int i = 0; i < count; ++i) {
        if (list[i] == window || !space_navigation_window(list[i])) continue;

        struct view *view = window_manager_find_managed_window(&g_window_manager, list[i]);
        if (!view) {
            unknown = true;
        } else if (space_navigation_spaces_contain(spaces, space_count, view->sid)) {
            return true;
        }
    }

    if (!unknown) return false;

    uint32_t ids[SPACE_NAVIGATION_WINDOWS_MAX];
    int id_count = space_navigation_spaces_windows(spaces, space_count, window->application->connection,
                                                   ids, SPACE_NAVIGATION_WINDOWS_MAX);

    for (int i = 0; i < id_count; ++i) {
        struct window *other = window_manager_find_window(&g_window_manager, ids[i]);
        if (other == window || !space_navigation_window(other)) continue;

        if (other->application == window->application) return true;
    }

    return false;
}

// The window navigation activates on a Desktop: its frontmost eligible one.
static struct window *space_navigation_candidate(uint32_t *ids, int count)
{
    for (int i = 0; i < count; ++i) {
        struct window *window = window_manager_find_window(&g_window_manager, ids[i]);
        if (space_navigation_window(window)) return window;
    }

    return NULL;
}

static void space_navigation_activate(struct window *focus, bool move, bool raise)
{
    if (move) {
        window_manager_focus_window_with_raise(&focus->application->psn, focus->id, focus->ref);
    } else if (raise) {
        // Raising completes before the next navigation can switch away, so
        // the application cannot raise it on a hidden space.
        space_navigation_raise_window(&focus->application->psn, focus->id, focus->ref);
    } else {
        // The selected window is already frontmost. AXRaise can block while
        // its application responds to the space switch.
        space_navigation_focus_window(&focus->application->psn, focus->id);
    }

    // The activation that follows need not ask the application, which is
    // busy with the switch, for its focused window.
    window_focus_note(focus->id);
    space_navigation_schedule_activated(focus->id);
}

// The steps before this one reached its Desktop without activating anything,
// so the window focused may still be one of a Desktop left behind.
static bool space_navigation_settle(uint64_t sid)
{
    if (mission_control_is_active()) return false;

    space_navigation_focus_cancel();

    int count = 0;
    uint32_t *ids = space_window_list(sid, &count, false);
    struct window *focus = ids ? space_navigation_candidate(ids, count) : NULL;

    if (focus && focus->id != g_window_manager.focused_window_id) {
        space_navigation_activate(focus, false, space_navigation_needs_raise(focus, space_navigation_space_display(sid)));
    }

    return true;
}

// A click since `time` wins over navigation that has not switched yet.
static bool space_navigation_clicked_since(uint64_t time)
{
    return space_navigation_seconds_since_click() * 1e9 < (double) (read_os_timer() - time);
}

// The daemon's settings over the effect a step was given. With navigation_effect
// off no step shows one: a zero duration takes every effect out and still switches.
//
// NOTE: Under memory pressure WindowServer can put the crossfade's window on
// screen after Dock has switched. We wait one refresh after ordering it; once
// it came about 110 ms later, so the destination showed, then the outgoing
// image, then the fade. No call tells us when the window is on screen, and on
// a 6016 x 3384 display the capture and the window's copy take 81 MB each,
// exactly when memory is short. So while macOS reports pressure a crossfade
// follows navigation_pressure_fallback: by default it becomes the veil, which
// captures nothing; `keep` accepts the risk, and `none` leaves the switch to
// Dock alone. The pressure is read only for a crossfade with a duration.
static void space_navigation_resolve_effect(struct space_navigation_step *step)
{
    if (!g_window_manager.navigation_effect) {
        step->duration = 0.0f;
        return;
    }

    if (!step->crossfade || step->duration <= 0.0f) return;

    int fallback = g_window_manager.navigation_pressure_fallback;
    if (fallback == SPACE_NAVIGATION_PRESSURE_KEEP || !space_navigation_memory_pressure()) return;

    if (fallback == SPACE_NAVIGATION_PRESSURE_VEIL) {
        step->crossfade = false;
        step->veil = true;
    } else {
        step->duration = 0.0f;
    }
}

// The crossfade and the veil cover the whole display, so both stay under
// Reduce Motion, whose own Desktop transition is a crossfade. They need a
// hidden destination and ordinary Desktops on both sides, and a duration: a
// fast request has none.
static bool space_navigation_overlays(uint64_t current, struct space_navigation_step *step)
{
    return (step->crossfade || step->veil) && step->duration > 0.0f && !space_navigation_space_visible(step->sid)
        && !space_navigation_space_fullscreen(step->sid) && !space_navigation_space_fullscreen(current);
}

// What a step decides before Dock switches: the destination's display, the
// window to activate and whether it needs a raise. The destination's windows
// are left in `ids` for a window fade. False when the step cannot switch.
static bool space_navigation_plan(uint64_t current, struct space_navigation_step *step,
                                  struct space_navigation_plan *plan, uint32_t **ids, int *count)
{
    uint64_t sid = step->sid;
    bool move = step->move;

    space_navigation_snapshot_cancel();
    // A deferred focus from the previous navigation must not follow this one.
    space_navigation_focus_cancel();

    uint32_t display = space_navigation_space_display(sid);
    if (mission_control_is_active() || display_manager_display_is_animating(display)) return false;

    struct window *focus = NULL;

    if (move) {
        focus = window_manager_focused_window(&g_window_manager);

        if (!space_navigation_window(focus) || space_navigation_space_fullscreen(sid)
            || window_check_flag(focus, WINDOW_FULLSCREEN)) {
            return false;
        }

        window_manager_send_window_to_space(&g_space_manager, &g_window_manager, focus, sid, false);
    }

    *count = 0;
    *ids = space_window_list(sid, count, false);
    if (!*ids) *count = 0;

    if (!focus) focus = space_navigation_candidate(*ids, *count);

    // Decided before the switch: afterwards WindowServer is busy showing the
    // new Desktop, and each query waits for about a frame.
    bool raise = focus && step->activate && (move || space_navigation_needs_raise(focus, display));

    *plan = (struct space_navigation_plan) {
        .current = current,
        .sid = sid,
        .display = display,
        .current_display = space_navigation_space_display(current),
        .focus_id = focus ? focus->id : 0,
        .move = move,
        .activate = step->activate,
        .raise = raise,
        .duration = step->duration,
        .now = read_os_timer()
    };

    return true;
}

// After Dock switched, or failed to: the report to the schedule, the
// activation and the anchor. The window is looked up again, since an
// asynchronous capture lets other events run after the plan. Presses queued
// meanwhile take the activation over, except from a moved window.
static void space_navigation_finish(struct space_navigation_plan *plan, bool success, float effect_duration)
{
    if (success) {
        space_navigation_last_time = plan->now;
        space_navigation_schedule_switched(effect_duration);
    }

    if (success && plan->activate && (plan->move || !space_navigation_schedule_defers_activation())) {
        struct window *focus = window_manager_find_window(&g_window_manager, plan->focus_id);

        if (plan->current_display != plan->display) {
            if (focus) {
                display_manager_set_active_display_id(plan->display);
                window_manager_center_mouse(&g_window_manager, focus);
            } else {
                display_manager_focus_display(plan->display, plan->sid);
            }
        }

        if (focus) space_navigation_activate(focus, plan->move, plan->raise);
    }

    if (success) {
        space_navigation_anchor.sid = plan->sid;
        space_navigation_anchor.time = read_os_timer();
    }
}

// Switches under the prepared overlay, if there is one: Dock's own switch,
// with only our overlay fading. Never apply alpha to Spaces: Finder content
// is shared between them.
static bool space_navigation_switch_overlay(struct space_navigation_plan *plan, bool prepared)
{
#ifdef YABAI_CAPTURE_DIAGNOSTICS
    uint64_t generation = space_snapshot_diag_active_generation();
#endif
    SNAP_DIAG("dock_request", generation, 0);
    bool success = scripting_addition_focus_space(plan->sid);
    SNAP_DIAG("dock_reply", generation, 0);
    bool started = prepared && space_navigation_snapshot_start(plan->duration, success);

    space_navigation_finish(plan, success, started ? plan->duration : 0.0f);
    return success;
}

// Switches with the destination's windows fading in from the step's alpha.
static bool space_navigation_switch_fade(struct space_navigation_plan *plan, struct space_navigation_step *step,
                                         uint32_t *ids, int count)
{
    uint64_t sid = plan->sid;
    float alpha = step->alpha;
    float duration = step->duration;

    // Repeated navigation stays immediate. Suppress effects, never navigation.
    bool fade = duration > 0.0f && alpha < 1.0f && !space_navigation_space_visible(sid)
        && !space_navigation_space_fullscreen(sid)
        && (space_navigation_last_time == 0 || plan->now - space_navigation_last_time >= 180000000ULL)
        && !space_navigation_reduce_motion();

    struct space_navigation_effect effect = { .count = 0 };
    bool effects_ok = space_navigation_prepare_effect(&effect, plan->display, ids, count, plan->focus_id, alpha, fade);

    // Unlike generic --focus, do not silently fall back to asynchronous
    // gestures after dimming windows: failure must restore opacity before
    // returning.
    bool success = effects_ok && scripting_addition_focus_space(sid);

    // Start the entire group before application activation or AXRaise can
    // block. Failure restores ordinary opacity without leaving dim windows.
    effects_ok = space_navigation_start_effect(&effect, plan->display,
        success ? plan->focus_id : g_window_manager.focused_window_id, alpha, duration, success) && effects_ok;

    space_navigation_finish(plan, success, 0.0f);
    return success && effects_ok;
}

// Runs a step whose effect is resolved to its end. A crossfade waits here for
// its capture, a veil for its two refreshes.
static bool space_navigation_run_resolved(uint64_t current, struct space_navigation_step *step)
{
    if (current == step->sid) return step->activate && step->settle ? space_navigation_settle(step->sid) : true;

    struct space_navigation_plan plan;
    uint32_t *ids;
    int count;
    if (!space_navigation_plan(current, step, &plan, &ids, &count)) return false;

    if (!step->crossfade && !step->veil) return space_navigation_switch_fade(&plan, step, ids, count);

    bool prepared = false;

    if (space_navigation_overlays(current, step)) {
        uint64_t capture_started = read_os_timer();
        float interval = space_navigation_frame_interval(plan.display);
        prepared = step->veil ? space_navigation_veil_prepare(plan.display, plan.sid, interval)
                              : space_navigation_snapshot_prepare(plan.display, plan.sid, interval);

        if (space_navigation_clicked_since(capture_started)) {
            space_navigation_snapshot_cancel();
            return false; // A newer pointer action wins over a slow capture.
        }
    }

    return space_navigation_switch_overlay(&plan, prepared);
}

static bool space_navigation_run_step(uint64_t current, struct space_navigation_step *step)
{
    space_navigation_resolve_effect(step);
    return space_navigation_run_resolved(current, step);
}

// The step waiting for its snapshot capture. Only the event loop touches it.
static struct
{
    bool active;
    int token;
    uint64_t capture_started;
    struct space_navigation_plan plan;
} space_navigation_flight;

// Starts a step. A crossfade requests its capture and returns
// SPACE_NAVIGATION_PENDING; space_navigation_step_captured switches when the
// capture comes back or its deadline passes, and reports the outcome to the
// schedule. Any other step, a veil included, runs to its end here.
static enum space_navigation_result space_navigation_begin_step(uint64_t current, struct space_navigation_step *step)
{
    space_navigation_resolve_effect(step);

    if (current == step->sid || !step->crossfade) {
        return space_navigation_run_resolved(current, step) ? SPACE_NAVIGATION_SWITCHED : SPACE_NAVIGATION_FAILED;
    }

    struct space_navigation_plan plan;
    uint32_t *ids;
    int count;
    if (!space_navigation_plan(current, step, &plan, &ids, &count)) return SPACE_NAVIGATION_FAILED;

    if (space_navigation_overlays(current, step)) {
        uint64_t capture_started = read_os_timer();
        int token = ++space_navigation_flight.token;
        if (!token) token = ++space_navigation_flight.token;

        if (space_navigation_snapshot_capture(plan.display, plan.sid, space_navigation_frame_interval(plan.display), token)) {
            space_navigation_flight.active = true;
            space_navigation_flight.capture_started = capture_started;
            space_navigation_flight.plan = plan;
            return SPACE_NAVIGATION_PENDING;
        }

        if (space_navigation_clicked_since(capture_started)) {
            space_navigation_snapshot_cancel();
            return SPACE_NAVIGATION_FAILED;
        }
    }

    return space_navigation_switch_overlay(&plan, false) ? SPACE_NAVIGATION_SWITCHED : SPACE_NAVIGATION_FAILED;
}

// Check before expensive presentation, and again afterwards: pointer input
// can arrive while the renderer draws or waits for a refresh.
static bool space_navigation_flight_stopped(struct space_navigation_plan *plan)
{
    return space_navigation_clicked_since(space_navigation_flight.capture_started)
        || mission_control_is_active() || display_manager_display_is_animating(plan->display);
}

// Completes exactly once, either for a callback/deadline or an accepted quick
// request. Giving up an optional image is MISSING, not a failed logical step.
static void space_navigation_complete_capture(int token, bool skip)
{
    if (!space_navigation_flight.active || token != space_navigation_flight.token) return;
    space_navigation_flight.active = false;

    struct space_navigation_plan plan = space_navigation_flight.plan;
    bool stopped = space_navigation_flight_stopped(&plan);
    enum space_snapshot_result result = skip || stopped ? space_navigation_snapshot_discard(token)
                                                        : space_navigation_snapshot_present(token);
    // Discard neither draws nor waits. Recheck only after presentation, which
    // can be slow even when surface preparation eventually fails.
    stopped = stopped || result == SPACE_SNAPSHOT_CANCELLED
        || (!skip && space_navigation_flight_stopped(&plan));
    bool success = false;

    if (!stopped) {
        success = space_navigation_switch_overlay(&plan, result == SPACE_SNAPSHOT_READY);
    } else if (result == SPACE_SNAPSHOT_READY) {
        space_navigation_snapshot_cancel();
    }

    space_navigation_schedule_completed(success);
}

// Event loop, when capture returned or reached its deadline. Cancelled or
// superseded tokens cannot present, switch or report a second completion.
void space_navigation_step_captured(int token)
{
    SNAP_DIAG("capture_event_loop", 0, token);
    space_navigation_complete_capture(token, false);
}

// Queue insertion has already preserved the new request. Retire the pending
// presentation and perform the existing planned switch/focus/completion now.
// Moving a window retains its effect and activation policy.
static void space_navigation_step_skip_effect(void)
{
    if (!space_navigation_flight.active || space_navigation_flight.plan.move) return;
    int token = space_navigation_flight.token;
    SNAP_DIAG("capture_skip", 0, token);
    space_navigation_complete_capture(token, true);
}

// The step in flight is abandoned: its capture, when it comes back, finds
// nothing to switch, and the schedule is not told.
static void space_navigation_step_cancel(void)
{
    space_navigation_flight.active = false;
}

// A newer explicit focus request wins after a click, without finishing the
// destination the click abandoned. The caller has already accepted the new
// input. Mission Control, display animation and window moves keep their policy.
static bool space_navigation_step_replace_after_click(uint64_t input_time)
{
    if (!space_navigation_flight.active || space_navigation_flight.plan.move
        || !space_navigation_clicked_since(space_navigation_flight.capture_started)
        || space_navigation_clicked_since(input_time)
        || mission_control_is_active()
        || display_manager_display_is_animating(space_navigation_flight.plan.display)) return false;

    space_navigation_step_cancel();
    space_navigation_snapshot_cancel();
    return true;
}
